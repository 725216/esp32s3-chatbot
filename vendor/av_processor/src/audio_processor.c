/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_chip_info.h"

#include "esp_gmf_element.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_audio_enc.h"
#include "esp_fourcc.h"
#include "esp_audio_enc_default.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_audio_types.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"
#include "av_pipeline_pool.h"
#include "esp_gmf_fifo.h"
#include "esp_gmf_new_databus.h"

#include "esp_vad.h"
#include "esp_afe_config.h"
#include "esp_gmf_wn.h"
#if CONFIG_AUDIO_HLS_ENABLE
#include "esp_hls_io.h"
#endif  /* CONFIG_AUDIO_HLS_ENABLE */
#if CONFIG_AUDIO_AFE_ENABLE && (CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31)
#include "esp_gmf_afe.h"
#endif  /* CONFIG_AUDIO_AFE_ENABLE */

#include "esp_audio_render.h"
#include "audio_processor.h"

#if CONFIG_AUDIO_PROCESSOR_IMPL_GMF
#include "media_lib_adapter.h"
#endif

#if CONFIG_MEDIA_DUMP_ENABLE
#include "media_dump.h"
#endif  /* CONFIG_MEDIA_DUMP_ENABLE */

#if CONFIG_AUDIO_AFE_ENABLE && (CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31)
#define AV_PROCESSOR_SUPPORT_AFE 1
#else
#define AV_PROCESSOR_SUPPORT_AFE 0
#endif

#define AUDIO_OPUS_PIP_TASK_STACK_SIZE (4096 * 10)
#define AUDIO_BUFFER_SIZE              (2048)
#define AUDIO_RINGBUFFER_SIZE          (131072) /* LOCAL FIX (2026-11-01 v25): 播放侧吸收缓冲。ringbuf 数据区经 esp_gmf_oal_calloc 分配在 PSRAM(CONFIG_SPIRAM_BOOT_INIT=y)，不占 internal。v24 曾减半省内存是误判(白改)。改 128KB(4s)：开播后 WS 慢期(21-27KB/s<32KB/s)缺口 50-110KB，由 128KB ringbuf 吸收，消除开头断续；供给<消费时本缓冲不会阻塞 pipeline */
#define AUDIO_FIFO_NUM                 (5)
#define AUDIO_MANAGER_MAX_PLAYERS      (4)
#define AUDIO_MANAGER_MAX_FEEDERS      (2)
#define SAFE_CLEANUP(handle, cleanup_fn) \
    do {                                 \
        if ((handle) != NULL) {          \
            cleanup_fn(handle);          \
            (handle) = NULL;             \
        }                                \
    } while (0)

static char *TAG = "AUDIO_PROCESSOR";

enum audio_render_stream_type_e {
    AUDIO_RENDER_STREAM_FEEDER   = 0,
    AUDIO_RENDER_STREAM_PLAYBACK = 1,
    AUDIO_RENDER_STREAM_TYPE_MAX = 2,
};

enum audio_play_state_e {
    AUDIO_RUN_STATE_IDLE,
    AUDIO_RUN_STATE_PLAYING,
    AUDIO_RUN_STATE_CLOSED,
    AUDIO_RUN_STATE_PAUSED,
};

typedef enum {
    AUDIO_RECORDER_FRONTEND_BYPASS = 0,
    AUDIO_RECORDER_FRONTEND_WN,
    AUDIO_RECORDER_FRONTEND_AFE,
} audio_recorder_frontend_t;

struct audio_play_s {
    esp_asp_handle_t           player;
    enum audio_play_state_e    state;
    esp_gmf_pool_handle_t      pool;
#if CONFIG_AUDIO_HLS_ENABLE
    uint32_t                   hls_last_format;
#endif  /* CONFIG_AUDIO_HLS_ENABLE */
    audio_play_config_t        config;
};

struct audio_recorder_s {
    esp_gmf_fifo_handle_t         fifo;
    enum audio_play_state_e       state;
    esp_gmf_task_handle_t         task;
    esp_gmf_pipeline_handle_t     pipe;
#if AV_PROCESSOR_SUPPORT_AFE
    esp_gmf_afe_manager_handle_t  afe_manager;
#endif  /* AV_PROCESSOR_SUPPORT_AFE */
    esp_gmf_pool_handle_t         pool;
#if AV_PROCESSOR_SUPPORT_AFE
    afe_config_t                 *afe_cfg;
#endif  /* AV_PROCESSOR_SUPPORT_AFE */
    srmodel_list_t               *models;
    audio_recorder_frontend_t     frontend;
    audio_recorder_wn_event_t     wn_event;
    audio_recorder_config_t       config;
};

struct audio_feeder_s {
    esp_gmf_pipeline_handle_t  pipe;
    esp_gmf_db_handle_t        fifo;
    esp_gmf_task_handle_t      task;
    esp_gmf_pool_handle_t      pool;
    audio_feeder_config_t      config;
    enum audio_play_state_e  state;
};

typedef struct {
    esp_gmf_pool_handle_t             pool;
    audio_manager_config_t            config;
    esp_audio_render_handle_t         render_handle;
    esp_audio_render_stream_handle_t  stream[AUDIO_RENDER_STREAM_TYPE_MAX];
    bool                              enable_mixer;
    audio_play_handle_t               players[AUDIO_MANAGER_MAX_PLAYERS];
    audio_feeder_handle_t             feeders[AUDIO_MANAGER_MAX_FEEDERS];
    audio_play_handle_t               mixer_playback;
    audio_feeder_handle_t             mixer_feeder;
} audio_manager_t;

static audio_manager_t  audio_manager;

static void audio_fill_gmf_task_cfg(esp_gmf_task_cfg_t *cfg, const audio_task_config_t *task_cfg,
                                    bool requires_large_stack, const char *task_name, const char *module_name);
static bool audio_play_is_prompt(audio_play_handle_t player);
static bool audio_play_is_playback(audio_play_handle_t player);

static bool audio_manager_register_player(audio_play_handle_t player)
{
    for (int i = 0; i < AUDIO_MANAGER_MAX_PLAYERS; i++) {
        if (audio_manager.players[i] == player) {
            return true;
        }
        if (audio_manager.players[i] == NULL) {
            audio_manager.players[i] = player;
            return true;
        }
    }
    ESP_LOGE(TAG, "No free player slot in audio manager");
    return false;
}

static void audio_manager_unregister_player(audio_play_handle_t player)
{
    for (int i = 0; i < AUDIO_MANAGER_MAX_PLAYERS; i++) {
        if (audio_manager.players[i] == player) {
            audio_manager.players[i] = NULL;
            break;
        }
    }
    if (audio_manager.mixer_playback == player) {
        audio_manager.mixer_playback = NULL;
    }
}

static bool audio_manager_register_feeder(audio_feeder_handle_t feeder)
{
    for (int i = 0; i < AUDIO_MANAGER_MAX_FEEDERS; i++) {
        if (audio_manager.feeders[i] == feeder) {
            return true;
        }
        if (audio_manager.feeders[i] == NULL) {
            audio_manager.feeders[i] = feeder;
            return true;
        }
    }
    ESP_LOGE(TAG, "No free feeder slot in audio manager");
    return false;
}

static void audio_manager_unregister_feeder(audio_feeder_handle_t feeder)
{
    for (int i = 0; i < AUDIO_MANAGER_MAX_FEEDERS; i++) {
        if (audio_manager.feeders[i] == feeder) {
            audio_manager.feeders[i] = NULL;
            break;
        }
    }
    if (audio_manager.mixer_feeder == feeder) {
        audio_manager.mixer_feeder = NULL;
    }
}

static bool audio_manager_any_prompt_playing(void)
{
    for (int i = 0; i < AUDIO_MANAGER_MAX_PLAYERS; i++) {
        audio_play_handle_t player = audio_manager.players[i];
        if (player && audio_play_is_prompt(player) && player->state == AUDIO_RUN_STATE_PLAYING) {
            return true;
        }
    }
    return false;
}

static bool audio_manager_any_playback_playing(void)
{
    for (int i = 0; i < AUDIO_MANAGER_MAX_PLAYERS; i++) {
        audio_play_handle_t player = audio_manager.players[i];
        if (player && audio_play_is_playback(player) && player->state == AUDIO_RUN_STATE_PLAYING) {
            return true;
        }
    }
    return false;
}

static bool audio_manager_any_feeder_playing(void)
{
    for (int i = 0; i < AUDIO_MANAGER_MAX_FEEDERS; i++) {
        audio_feeder_handle_t feeder = audio_manager.feeders[i];
        if (feeder && feeder->state == AUDIO_RUN_STATE_PLAYING) {
            return true;
        }
    }
    return false;
}

static const char *audio_play_type_to_str(audio_play_type_t type)
{
    switch (type) {
        case AUDIO_PLAY_TYPE_PROMPT:   return "prompt";
        case AUDIO_PLAY_TYPE_PLAYBACK:
        default:                       return "playback";
    }
}

static bool audio_play_is_prompt(audio_play_handle_t player)
{
    return player && player->config.type == AUDIO_PLAY_TYPE_PROMPT;
}

static bool audio_play_is_playback(audio_play_handle_t player)
{
    return player && player->config.type == AUDIO_PLAY_TYPE_PLAYBACK;
}

static const char *audio_recorder_frontend_to_str(audio_recorder_frontend_t frontend)
{
    switch (frontend) {
        case AUDIO_RECORDER_FRONTEND_WN:  return "ai_wn";
        case AUDIO_RECORDER_FRONTEND_AFE: return "ai_afe";
        case AUDIO_RECORDER_FRONTEND_BYPASS:
        default:                          return "bypass";
    }
}

static bool recorder_target_support_wn(void)
{
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 || \
    CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C6 || \
    CONFIG_IDF_TARGET_ESP32S2
    return true;
#else
    return false;
#endif
}

static bool recorder_target_support_afe(void)
{
#if AV_PROCESSOR_SUPPORT_AFE
    return true;
#else
    return false;
#endif
}

static bool recorder_frontend_uses_processed_pcm(audio_recorder_frontend_t frontend)
{
    return frontend != AUDIO_RECORDER_FRONTEND_BYPASS;
}

static const char *recorder_get_input_format(void)
{
    static char fallback_layout[8] = {0};

    if (audio_manager.config.mic_layout[0] != '\0') {
        return audio_manager.config.mic_layout;
    }
    int channels = audio_manager.config.board_channels;
    if (channels <= 0) {
        channels = 1;
    }
    if (channels > (int)(sizeof(fallback_layout) - 1)) {
        channels = sizeof(fallback_layout) - 1;
    }
    memset(fallback_layout, 'M', channels);
    fallback_layout[channels] = '\0';
    return fallback_layout;
}

static uint32_t recorder_get_afe_algo_mask(const av_processor_afe_config_t *cfg)
{
    return cfg ? cfg->mode.afe.algo_mask : 0;
}

static uint32_t recorder_get_custom_algo_mask(const av_processor_afe_config_t *cfg)
{
    return cfg ? cfg->mode.custom.algo_mask : 0;
}

static esp_err_t recorder_select_frontend(audio_recorder_handle_t rec, audio_recorder_frontend_t *frontend)
{
    const av_processor_afe_config_t *cfg = &rec->config.afe_config;
    bool want_afe = cfg && cfg->frontend_type == AV_PROCESSOR_FRONTEND_TYPE_AFE;
    uint32_t afe_algo_mask = want_afe ? recorder_get_afe_algo_mask(cfg) : 0;
    uint32_t custom_algo_mask = want_afe ? 0 : recorder_get_custom_algo_mask(cfg);
    bool want_wakenet = (custom_algo_mask & AV_PROCESSOR_CUSTOM_FLAG_WAKENET_ENABLE) != 0;
    bool has_afe_request = afe_algo_mask != 0;
    bool explicit_request = want_afe || want_wakenet || has_afe_request;

    if (!frontend) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!explicit_request) {
        *frontend = AUDIO_RECORDER_FRONTEND_BYPASS;
        return ESP_OK;
    }

    if (want_afe && want_wakenet) {
        ESP_LOGE(TAG, "Built-in ai_afe and standalone ai_wn are mutually exclusive");
        return ESP_ERR_INVALID_ARG;
    }

    if (want_afe) {
        if (!recorder_target_support_afe()) {
            ESP_LOGE(TAG, "Requested ai_afe, but current target does not support it");
            return ESP_ERR_NOT_SUPPORTED;
        }
        *frontend = AUDIO_RECORDER_FRONTEND_AFE;
        return ESP_OK;
    }

    if (has_afe_request) {
        ESP_LOGE(TAG, "AFE flags were set, but `frontend_type` is not `AV_PROCESSOR_FRONTEND_TYPE_AFE`");
        return ESP_ERR_INVALID_ARG;
    }

    if (custom_algo_mask & ~AV_PROCESSOR_CUSTOM_FLAG_WAKENET_ENABLE) {
        ESP_LOGE(TAG, "Unsupported custom algorithm mask: 0x%" PRIx32, custom_algo_mask);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (want_wakenet) {
        if (!recorder_target_support_wn()) {
            ESP_LOGE(TAG, "Requested WakeNet, but current target does not support ai_wn");
            return ESP_ERR_NOT_SUPPORTED;
        }
        *frontend = AUDIO_RECORDER_FRONTEND_WN;
        return ESP_OK;
    }

    *frontend = AUDIO_RECORDER_FRONTEND_BYPASS;
    return ESP_OK;
}

static esp_err_t recorder_validate_frontend(audio_recorder_frontend_t frontend)
{
    if (frontend == AUDIO_RECORDER_FRONTEND_WN &&
        (audio_manager.config.board_sample_rate != 16000 || audio_manager.config.board_bits != 16)) {
        ESP_LOGE(TAG, "ai_wn currently requires 16 kHz / 16-bit input, board format is %d Hz / %d-bit",
                 audio_manager.config.board_sample_rate, audio_manager.config.board_bits);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static inline esp_opus_enc_frame_duration_t convert_opus_dec_frame_duration(int frame_duration)
{
    switch (frame_duration) {
        case 2:
            return ESP_OPUS_DEC_FRAME_DURATION_2_5_MS;
        case 5:
            return ESP_OPUS_DEC_FRAME_DURATION_5_MS;
        case 10:
            return ESP_OPUS_DEC_FRAME_DURATION_10_MS;
        case 20:
            return ESP_OPUS_DEC_FRAME_DURATION_20_MS;
        case 40:
            return ESP_OPUS_DEC_FRAME_DURATION_40_MS;
        case 60:
            return ESP_OPUS_DEC_FRAME_DURATION_60_MS;
        case 80:
            return ESP_OPUS_DEC_FRAME_DURATION_80_MS;
        case 100:
            return ESP_OPUS_DEC_FRAME_DURATION_100_MS;
        case 120:
            return ESP_OPUS_DEC_FRAME_DURATION_120_MS;
        default:
            return ESP_OPUS_DEC_FRAME_DURATION_INVALID;
    }
}

static inline esp_opus_enc_frame_duration_t convert_opus_enc_frame_duration(int frame_duration)
{
    switch (frame_duration) {
        case 2:
            return ESP_OPUS_ENC_FRAME_DURATION_2_5_MS;
        case 5:
            return ESP_OPUS_ENC_FRAME_DURATION_5_MS;
        case 10:
            return ESP_OPUS_ENC_FRAME_DURATION_10_MS;
        case 20:
            return ESP_OPUS_ENC_FRAME_DURATION_20_MS;
        case 40:
            return ESP_OPUS_ENC_FRAME_DURATION_40_MS;
        case 60:
            return ESP_OPUS_ENC_FRAME_DURATION_60_MS;
        case 80:
            return ESP_OPUS_ENC_FRAME_DURATION_80_MS;
        case 100:
            return ESP_OPUS_ENC_FRAME_DURATION_100_MS;
        case 120:
            return ESP_OPUS_ENC_FRAME_DURATION_120_MS;
        default:
            return ESP_OPUS_ENC_FRAME_DURATION_ARG;
    }
}

static esp_err_t set_pipeline_dest_rate_if_present(esp_gmf_pipeline_handle_t pipe, const char *el_name, int rate)
{
    esp_gmf_obj_handle_t rate_cvt = NULL;
    esp_err_t ret = esp_gmf_pipeline_get_el_by_name(pipe, el_name, &rate_cvt);
    if (ret == ESP_OK && rate_cvt) {
        ret = esp_gmf_rate_cvt_set_dest_rate(rate_cvt, rate);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set rate converter destination rate");
    }
    return ESP_OK;
}

static esp_err_t set_pipeline_dest_channel_if_present(esp_gmf_pipeline_handle_t pipe, const char *el_name, int channel)
{
    esp_gmf_element_handle_t ch_cvt = NULL;
    esp_err_t ret = esp_gmf_pipeline_get_el_by_name(pipe, el_name, &ch_cvt);
    if (ret == ESP_OK && ch_cvt) {
        ret = esp_gmf_ch_cvt_set_dest_channel(ch_cvt, channel);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set channel converter channels");
    }
    return ESP_OK;
}

static esp_err_t set_pipeline_dest_bits_if_present(esp_gmf_pipeline_handle_t pipe, const char *el_name, int bits)
{
    esp_gmf_element_handle_t bit_cvt = NULL;
    esp_err_t ret = esp_gmf_pipeline_get_el_by_name(pipe, el_name, &bit_cvt);
    if (ret == ESP_OK && bit_cvt) {
        ret = esp_gmf_bit_cvt_set_dest_bits(bit_cvt, bits);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set bit converter bits");
    }
    return ESP_OK;
}

#if CONFIG_AUDIO_HLS_ENABLE
static int audio_play_get_simple_player_dest_rate(void)
{
#ifdef CONFIG_AUDIO_SIMPLE_PLAYER_RESAMPLE_DEST_RATE
    return CONFIG_AUDIO_SIMPLE_PLAYER_RESAMPLE_DEST_RATE;
#else
    return audio_manager.config.board_sample_rate > 0 ? audio_manager.config.board_sample_rate : 16000;
#endif
}

static int audio_play_get_simple_player_dest_channels(void)
{
#ifdef CONFIG_AUDIO_SIMPLE_PLAYER_CH_CVT_DEST
    return CONFIG_AUDIO_SIMPLE_PLAYER_CH_CVT_DEST;
#else
    return audio_manager.config.board_channels > 0 ? audio_manager.config.board_channels : 1;
#endif
}

static int audio_play_get_simple_player_dest_bits(void)
{
#ifdef CONFIG_AUDIO_SIMPLE_PLAYER_BIT_CVT_DEST_24BIT
    return 24;
#elif defined(CONFIG_AUDIO_SIMPLE_PLAYER_BIT_CVT_DEST_32BIT)
    return 32;
#elif defined(CONFIG_AUDIO_SIMPLE_PLAYER_BIT_CVT_DEST_16BIT)
    return 16;
#else
    return audio_manager.config.board_bits > 0 ? audio_manager.config.board_bits : 16;
#endif
}

static int audio_play_hls_media_type_cb(esp_hls_file_seg_info_t *info, void *ctx)
{
    audio_play_handle_t player = (audio_play_handle_t)ctx;
    if (!player || !player->player || !info) {
        return ESP_ERR_INVALID_ARG;
    }
    if (player->hls_last_format == info->format) {
        return ESP_OK;
    }

    esp_gmf_pipeline_handle_t pipeline = NULL;
    esp_gmf_element_handle_t dec_el = NULL;
    esp_err_t ret = esp_audio_simple_player_get_pipeline(player->player, &pipeline);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to get simple player pipeline for HLS");
    ret = esp_gmf_pipeline_get_el_by_name(pipeline, "aud_dec", &dec_el);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to get decoder element for HLS");

    esp_gmf_info_sound_t music_info = {
        .sample_rates = audio_play_get_simple_player_dest_rate(),
        .channels = audio_play_get_simple_player_dest_channels(),
        .bits = audio_play_get_simple_player_dest_bits(),
        .format_id = info->format,
    };
    if (info->format == ESP_FOURCC_TO_INT('T', 'S', ' ', ' ')) {
        music_info.format_id = ESP_FOURCC_M2TS;
    }

    ret = esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &music_info);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to reconfigure decoder for HLS segment");
    player->hls_last_format = info->format;
    ESP_LOGI(TAG, "HLS segment format detected: %s", ESP_FOURCC_TO_STR(info->format));
    return ESP_OK;
}

static esp_err_t audio_play_register_hls_io(audio_play_handle_t player)
{
    esp_gmf_pool_handle_t player_pool = NULL;
    esp_gmf_io_handle_t hls_io = NULL;
    esp_err_t ret = esp_audio_simple_player_get_pool(player->player, &player_pool);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to get simple player pool");

    esp_hls_io_cfg_t hls_cfg = {
        .pool = player_pool,
        .name = "io_hls",
        .file_seg_cb = audio_play_hls_media_type_cb,
        .ctx = player,
    };
    ret = esp_gmf_io_hls_init(&hls_cfg, &hls_io);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to initialize HLS IO");

    ret = esp_audio_simple_player_register_io(player->player, hls_io);
    if (ret != ESP_OK) {
        esp_gmf_obj_delete(hls_io);
        ESP_LOGE(TAG, "Failed to register HLS IO");
        return ESP_FAIL;
    }
    return ESP_OK;
}
#endif  /* CONFIG_AUDIO_HLS_ENABLE */

static esp_err_t audio_manager_write_output(uint8_t *data, int data_size)
{
    if (audio_manager.config.play_io.write_cb) {
        return audio_manager.config.play_io.write_cb(data, data_size, audio_manager.config.play_io.write_ctx);
    }
    ESP_LOGE(TAG, "No playback output is configured");
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t audio_manager_read_input(uint8_t *data, int data_size)
{
    if (audio_manager.config.rec_io.read_cb) {
        return audio_manager.config.rec_io.read_cb(data, data_size, audio_manager.config.rec_io.read_ctx);
    }
    ESP_LOGE(TAG, "No record input is configured");
    return ESP_ERR_INVALID_STATE;
}

static int audio_render_write_hdlr(uint8_t *pcm_data, uint32_t len, void *ctx)
{
    bool playback_playing = audio_manager_any_playback_playing();
    bool feeder_playing = audio_manager_any_feeder_playing();
    bool prompt_playing = audio_manager_any_prompt_playing();
    if ((!playback_playing && !feeder_playing) || prompt_playing) {
        ESP_LOGD(TAG, "Audio playback and feeder are not playing, skip write");
        return ESP_OK;
    }
    ESP_LOGD(TAG, "Audio render write handler, data size: %ld", len);
    return audio_manager_write_output(pcm_data, len);
}

static int audio_play_out_data_callback(uint8_t *data, int data_size, void *ctx)
{
    audio_play_handle_t player = (audio_play_handle_t)ctx;
    if (audio_play_is_prompt(player)) {
        return audio_manager_write_output(data, data_size);
    }
    bool prompt_playing = audio_manager_any_prompt_playing();
    if (player->state != AUDIO_RUN_STATE_PLAYING || prompt_playing) {
        ESP_LOGD(TAG, "Audio playback is not playing, skip write");
        return ESP_OK;
    }
    if (audio_manager.enable_mixer) {
        if (audio_manager.mixer_playback != player) {
            ESP_LOGD(TAG, "Playback handle is not bound to mixer, skip write");
            return ESP_OK;
        }
        esp_audio_render_stream_write(audio_manager.stream[AUDIO_RENDER_STREAM_PLAYBACK], data, data_size);
    } else {
        return audio_manager_write_output(data, data_size);
    }
    return ESP_OK;
}

static int audio_play_event_callback(esp_asp_event_pkt_t *event, void *ctx)
{
    audio_play_handle_t player = (audio_play_handle_t)ctx;
    if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
        esp_asp_music_info_t info = {0};
        memcpy(&info, event->payload, event->payload_size);
        ESP_LOGI(TAG, "Get info, rate:%d, channels:%d, bits:%d", info.sample_rate, info.channels, info.bits);
    } else if (event->type == ESP_ASP_EVENT_TYPE_STATE) {
        esp_asp_state_t st = 0;
        memcpy(&st, event->payload, event->payload_size);
        ESP_LOGI(TAG, "%s event: Get State, %d,%s", audio_play_type_to_str(player->config.type),
                 st, esp_audio_simple_player_state_to_str(st));
        if (((st == ESP_ASP_STATE_STOPPED) || (st == ESP_ASP_STATE_FINISHED) || (st == ESP_ASP_STATE_ERROR))) {
            player->state = AUDIO_RUN_STATE_IDLE;
        }
        if (audio_play_is_playback(player) && player->config.event_cb) {
            audio_play_state_t state = AUDIO_PLAY_STATE_IDLE;
            if (st == ESP_ASP_STATE_STOPPED) {
                state = AUDIO_PLAY_STATE_STOPPED;
            } else if (st == ESP_ASP_STATE_FINISHED) {
                state = AUDIO_PLAY_STATE_FINISHED;
            } else if (st == ESP_ASP_STATE_ERROR) {
                state = AUDIO_PLAY_STATE_ERROR;
            } else if (st == ESP_ASP_STATE_RUNNING) {
                state = AUDIO_PLAY_STATE_PLAYING;
            } else if (st == ESP_ASP_STATE_PAUSED) {
                state = AUDIO_PLAY_STATE_PAUSED;
            }
            player->config.event_cb(state, player->config.event_cb_ctx);
        }
    }
    return ESP_OK;
}

static esp_err_t recorder_pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGD(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             OBJ_GET_TAG(event->from), event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    return ESP_OK;
}

static int recorder_outport_acquire_write(void *handle, esp_gmf_data_bus_block_t *blk, int wanted_size, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

static int recorder_outport_release_write(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    audio_recorder_handle_t rec = (audio_recorder_handle_t)handle;
    esp_gmf_data_bus_block_t _blk = {0};
    int ret = esp_gmf_fifo_acquire_write(rec->fifo, &_blk, blk->valid_size, block_ticks);
    if (ret < 0) {
        ESP_LOGE(TAG, "%s|%d, Fifo acquire write failed, ret: %d", __func__, __LINE__, ret);
        if (ret == ESP_GMF_IO_ABORT) {
            return ESP_GMF_ERR_ABORT;
        }
        return ESP_FAIL;
    }
    memcpy(_blk.buf, blk->buf, blk->valid_size);

    _blk.valid_size = blk->valid_size;
    ret = esp_gmf_fifo_release_write(rec->fifo, &_blk, block_ticks);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Fifo release write failed");
        if (ret == ESP_GMF_IO_ABORT) {
            return ESP_GMF_ERR_ABORT;
        }
        return ESP_FAIL;
    }
    return blk->valid_size;
}

static int recorder_inport_acquire_read(void *handle, esp_gmf_data_bus_block_t *blk, int wanted_size, int block_ticks)
{
    esp_err_t ret = audio_manager_read_input(blk->buf, wanted_size);
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }
    blk->valid_size = wanted_size;
#if CONFIG_MEDIA_DUMP_ENABLE && CONFIG_MEDIA_DUMP_AUDIO_BEFORE_AEC
    media_dump_feed(NULL, (uint8_t *)blk->buf, blk->valid_size);
#endif  /* CONFIG_MEDIA_DUMP_ENABLE && CONFIG_MEDIA_DUMP_AUDIO_BEFORE_AEC */
    return ESP_GMF_IO_OK;
}

static int recorder_inport_release_read(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

static void esp_gmf_wn_event_cb(esp_gmf_element_handle_t handle, int32_t trigger_ch, void *user_data)
{
    (void)handle;
    audio_recorder_handle_t rec = (audio_recorder_handle_t)user_data;
    rec->wn_event.trigger_ch = trigger_ch;
    ESP_LOGI(TAG, "Wake word detected on channel %" PRIi32, trigger_ch);
    if (rec->config.recorder_event_cb) {
        rec->config.recorder_event_cb(rec, &rec->wn_event, rec->config.recorder_ctx);
    }
}

#if AV_PROCESSOR_SUPPORT_AFE
static void esp_gmf_afe_event_cb(esp_gmf_obj_handle_t obj, esp_gmf_afe_evt_t *event, void *user_data)
{
    audio_recorder_handle_t rec = (audio_recorder_handle_t)user_data;
    if (rec->config.recorder_event_cb) {
        rec->config.recorder_event_cb(rec, (void *)event, rec->config.recorder_ctx);
    }
    av_processor_afe_config_t *cfg = &rec->config.afe_config;
    uint32_t afe_algo_mask = recorder_get_afe_algo_mask(cfg);
    bool wakeup_mode = (afe_algo_mask & AV_PROCESSOR_AFE_FLAG_AI_MODE_WAKEUP) != 0;
    bool handle_vcmd = wakeup_mode && (afe_algo_mask & AV_PROCESSOR_AFE_FLAG_VCMD_DETECT_ENABLE);

    switch (event->type) {
        case ESP_GMF_AFE_EVT_WAKEUP_START: {
            if (handle_vcmd) {
                esp_gmf_afe_vcmd_detection_cancel(obj);
                esp_gmf_afe_vcmd_detection_begin(obj);
            }
            esp_gmf_afe_wakeup_info_t *info = event->event_data;
            ESP_LOGI(TAG, "WAKEUP_START [%d : %d]", info->wake_word_index, info->wakenet_model_index);
            break;
        }
        case ESP_GMF_AFE_EVT_WAKEUP_END: {
            if (handle_vcmd) {
                esp_gmf_afe_vcmd_detection_cancel(obj);
            }
            ESP_LOGD(TAG, "WAKEUP_END");
            break;
        }
        case ESP_GMF_AFE_EVT_VAD_START: {
            if (handle_vcmd) {
                esp_gmf_afe_vcmd_detection_cancel(obj);
                esp_gmf_afe_vcmd_detection_begin(obj);
            }
            ESP_LOGD(TAG, "VAD_START");
            break;
        }
        case ESP_GMF_AFE_EVT_VAD_END: {
            if (handle_vcmd) {
                esp_gmf_afe_vcmd_detection_cancel(obj);
            }
            ESP_LOGD(TAG, "VAD_END");
            break;
        }
        case ESP_GMF_AFE_EVT_VCMD_DECT_TIMEOUT: {
            ESP_LOGD(TAG, "VCMD_DECT_TIMEOUT");
            break;
        }
        default: {
            esp_gmf_afe_vcmd_info_t *info = event->event_data;
            ESP_LOGW(TAG, "Command %d, phrase_id %d, prob %f, str: %s",
                     event->type, info->phrase_id, info->prob, info->str);
            break;
        }
    }
}

static esp_err_t recorder_setup_afe_config(audio_recorder_handle_t rec)
{
    av_processor_afe_config_t *cfg = &rec->config.afe_config;
    uint32_t afe_algo_mask = recorder_get_afe_algo_mask(cfg);
    bool wakeup_mode = (afe_algo_mask & AV_PROCESSOR_AFE_FLAG_AI_MODE_WAKEUP) != 0;
    bool aec_enable = (afe_algo_mask & AV_PROCESSOR_AFE_FLAG_AEC_ENABLE) != 0;
    bool ns_enable = (afe_algo_mask & AV_PROCESSOR_AFE_FLAG_NS_ENABLE) != 0;
    bool vad_enable = (afe_algo_mask & AV_PROCESSOR_AFE_FLAG_VAD_ENABLE) != 0;
    bool agc_enable = (afe_algo_mask & AV_PROCESSOR_AFE_FLAG_AGC_ENABLE) != 0;
    bool vcmd_enable = (afe_algo_mask & AV_PROCESSOR_AFE_FLAG_VCMD_DETECT_ENABLE) != 0;

    rec->models = esp_srmodel_init(cfg && cfg->model ? cfg->model : "model");
    if (!rec->models) {
        if (cfg && (wakeup_mode || vcmd_enable)) {
            ESP_LOGE(TAG, "Failed to initialize SR model for requested wakeup or command detection");
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "No voice wakeup model found");
    }
    rec->afe_cfg = afe_config_init(recorder_get_input_format(), rec->models,
                                   cfg->mode.afe.afe_type, cfg->mode.afe.afe_mode);
    if (!rec->afe_cfg) {
        ESP_LOGE(TAG, "Failed to initialize AFE config");
        return ESP_FAIL;
    }
    rec->afe_cfg->aec_init = aec_enable;
    rec->afe_cfg->se_init = ns_enable;
    rec->afe_cfg->vad_init = vad_enable;
    if (rec->afe_cfg->vad_init) {
        rec->afe_cfg->vad_mode = cfg ? cfg->mode.afe.vad_mode : 4;
        rec->afe_cfg->vad_min_speech_ms = cfg ? cfg->mode.afe.vad_min_speech_ms : 64;
        rec->afe_cfg->vad_min_noise_ms = cfg ? cfg->mode.afe.vad_min_noise_ms : 1000;
    }
    rec->afe_cfg->agc_init = agc_enable;
    rec->afe_cfg->wakenet_init = wakeup_mode;

    esp_gmf_afe_manager_cfg_t afe_manager_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(rec->afe_cfg, NULL, NULL, NULL, NULL);
    if (rec->config.afe_feed_task_config.task_stack > 0) {
        afe_manager_cfg.feed_task_setting.stack_size = rec->config.afe_feed_task_config.task_stack;
    }
    if (rec->config.afe_feed_task_config.task_prio > 0) {
        afe_manager_cfg.feed_task_setting.prio = rec->config.afe_feed_task_config.task_prio;
    }
    if (rec->config.afe_feed_task_config.task_core >= 0) {
        afe_manager_cfg.feed_task_setting.core = rec->config.afe_feed_task_config.task_core;
    }
    if (rec->config.afe_fetch_task_config.task_stack > 0) {
        afe_manager_cfg.fetch_task_setting.stack_size = rec->config.afe_fetch_task_config.task_stack;
    }
    if (rec->config.afe_fetch_task_config.task_prio > 0) {
        afe_manager_cfg.fetch_task_setting.prio = rec->config.afe_fetch_task_config.task_prio;
    }
    if (rec->config.afe_fetch_task_config.task_core >= 0) {
        afe_manager_cfg.fetch_task_setting.core = rec->config.afe_fetch_task_config.task_core;
    }
    esp_err_t ret = esp_gmf_afe_manager_create(&afe_manager_cfg, &rec->afe_manager);
    if (ret != ESP_OK) {
        afe_config_free(rec->afe_cfg);
        rec->afe_cfg = NULL;
        ESP_LOGE(TAG, "Failed to create AFE manager");
        return ESP_FAIL;
    }
    esp_gmf_element_handle_t gmf_afe = NULL;
    esp_gmf_afe_cfg_t gmf_afe_cfg = DEFAULT_GMF_AFE_CFG(rec->afe_manager, esp_gmf_afe_event_cb, rec, rec->models);
    gmf_afe_cfg.vcmd_detect_en = vcmd_enable;
    if (gmf_afe_cfg.vcmd_detect_en) {
        gmf_afe_cfg.vcmd_timeout = cfg ? cfg->mode.afe.vcmd_timeout_ms : 5000;
    }
    if (cfg && wakeup_mode) {
        gmf_afe_cfg.mn_language = cfg->mode.afe.mn_language ? cfg->mode.afe.mn_language : "cn";
        gmf_afe_cfg.wakeup_end = cfg->mode.afe.wakeup_end_time_ms;
        gmf_afe_cfg.wakeup_time = cfg->mode.afe.wakeup_time_ms;
    } else {
        gmf_afe_cfg.wakeup_end = 0;
        gmf_afe_cfg.wakeup_time = 0;
    }

    ret = esp_gmf_afe_init(&gmf_afe_cfg, &gmf_afe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto _quit;}, "Failed to initialize AFE element");

    afe_config_free(rec->afe_cfg);
    rec->afe_cfg = NULL;

    esp_gmf_pool_register_element(rec->pool, gmf_afe, NULL);
    return ESP_OK;

_quit:
    SAFE_CLEANUP(rec->afe_cfg, afe_config_free);
    SAFE_CLEANUP(rec->afe_manager, esp_gmf_afe_manager_destroy);
    return ret;
}
#endif  /* AV_PROCESSOR_SUPPORT_AFE */

static esp_err_t recorder_setup_wn_config(audio_recorder_handle_t rec)
{
    av_processor_afe_config_t *cfg = &rec->config.afe_config;

    rec->models = esp_srmodel_init(cfg && cfg->model ? cfg->model : "model");
    if (!rec->models) {
        ESP_LOGE(TAG, "Failed to initialize SR model for WakeNet");
        return ESP_FAIL;
    }
    esp_gmf_wn_cfg_t gmf_wn_cfg = {
        .models = rec->models,
        .input_format = (char *)recorder_get_input_format(),
    };
    esp_gmf_element_handle_t gmf_wn = NULL;
    esp_err_t ret = esp_gmf_wn_init(&gmf_wn_cfg, &gmf_wn);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to initialize WN element");
    ret = esp_gmf_wn_set_detect_cb(gmf_wn, esp_gmf_wn_event_cb, rec);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to set WN detect callback");
    ret = esp_gmf_pool_register_element(rec->pool, gmf_wn, NULL);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to register WN element");
    return ESP_OK;
}

static void recorder_get_pipeline_config(audio_recorder_frontend_t frontend,
                                         const av_processor_encoder_config_t *encoder_cfg,
                                         const char ***elements, int *element_count,
                                         const char **input_name, const char **output_name)
{
    switch (frontend) {
#if AV_PROCESSOR_SUPPORT_AFE
        case AUDIO_RECORDER_FRONTEND_AFE:
        {
            static const char *afe_pipeline_g711[] = {"ai_afe", "aud_rate_cvt", "aud_enc"};
            static const char *afe_pipeline_opus[] = {"ai_afe", "aud_enc"};
            static const char *afe_pipeline_aac[] = {"ai_afe", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt", "aud_enc"};
            static const char *afe_pipeline_pcm[] = {"ai_afe", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt", "aud_enc"};

            switch (encoder_cfg->format) {
                case AV_PROCESSOR_FORMAT_ID_G711A:
                case AV_PROCESSOR_FORMAT_ID_G711U:
                    *elements = afe_pipeline_g711;
                    *element_count = sizeof(afe_pipeline_g711) / sizeof(afe_pipeline_g711[0]);
                    break;
                case AV_PROCESSOR_FORMAT_ID_OPUS:
                    *elements = afe_pipeline_opus;
                    *element_count = sizeof(afe_pipeline_opus) / sizeof(afe_pipeline_opus[0]);
                    break;
                case AV_PROCESSOR_FORMAT_ID_AAC:
                    *elements = afe_pipeline_aac;
                    *element_count = sizeof(afe_pipeline_aac) / sizeof(afe_pipeline_aac[0]);
                    break;
                case AV_PROCESSOR_FORMAT_ID_PCM:
                default:
                    *elements = afe_pipeline_pcm;
                    *element_count = sizeof(afe_pipeline_pcm) / sizeof(afe_pipeline_pcm[0]);
                    break;
            }
            *input_name = "ai_afe";
            *output_name = (*elements)[*element_count - 1];
            break;
        }
#endif  /* AV_PROCESSOR_SUPPORT_AFE */
        case AUDIO_RECORDER_FRONTEND_WN:
        {
            static const char *wn_pipeline_g711[] = {"ai_wn", "aud_rate_cvt", "aud_enc"};
            static const char *wn_pipeline_opus[] = {"ai_wn", "aud_enc"};
            static const char *wn_pipeline_aac[] = {"ai_wn", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt", "aud_enc"};
            static const char *wn_pipeline_pcm[] = {"ai_wn", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt", "aud_enc"};

            switch (encoder_cfg->format) {
                case AV_PROCESSOR_FORMAT_ID_G711A:
                case AV_PROCESSOR_FORMAT_ID_G711U:
                    *elements = wn_pipeline_g711;
                    *element_count = sizeof(wn_pipeline_g711) / sizeof(wn_pipeline_g711[0]);
                    break;
                case AV_PROCESSOR_FORMAT_ID_OPUS:
                    *elements = wn_pipeline_opus;
                    *element_count = sizeof(wn_pipeline_opus) / sizeof(wn_pipeline_opus[0]);
                    break;
                case AV_PROCESSOR_FORMAT_ID_AAC:
                    *elements = wn_pipeline_aac;
                    *element_count = sizeof(wn_pipeline_aac) / sizeof(wn_pipeline_aac[0]);
                    break;
                case AV_PROCESSOR_FORMAT_ID_PCM:
                default:
                    *elements = wn_pipeline_pcm;
                    *element_count = sizeof(wn_pipeline_pcm) / sizeof(wn_pipeline_pcm[0]);
                    break;
            }
            *input_name = "ai_wn";
            *output_name = (*elements)[*element_count - 1];
            break;
        }
        case AUDIO_RECORDER_FRONTEND_BYPASS:
        default:
        {
            static const char *pipeline_pcm[] = {"aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt", "aud_enc"};
            *elements = pipeline_pcm;
            *element_count = sizeof(pipeline_pcm) / sizeof(pipeline_pcm[0]);
            *input_name = (*elements)[0];
            *output_name = (*elements)[*element_count - 1];
            break;
        }
    }
}

static esp_err_t recorder_setup_pipeline_ports(audio_recorder_handle_t rec, const char *input_name, const char *output_name)
{
    esp_gmf_port_handle_t outport = NEW_ESP_GMF_PORT_OUT_BYTE(
        recorder_outport_acquire_write,
        recorder_outport_release_write,
        NULL, rec, AUDIO_BUFFER_SIZE, portMAX_DELAY);

    esp_err_t ret = esp_gmf_pipeline_reg_el_port(rec->pipe, output_name, ESP_GMF_IO_DIR_WRITER, outport);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to register output port");

    esp_gmf_port_handle_t inport = NEW_ESP_GMF_PORT_IN_BYTE(
        recorder_inport_acquire_read,
        recorder_inport_release_read,
        NULL, rec, AUDIO_BUFFER_SIZE, portMAX_DELAY);

    ret = esp_gmf_pipeline_reg_el_port(rec->pipe, input_name, ESP_GMF_IO_DIR_READER, inport);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to register input port");

    return ESP_OK;
}

static esp_err_t recorder_setup_task_and_run(audio_recorder_handle_t rec)
{
    esp_gmf_task_cfg_t cfg = {0};
    audio_fill_gmf_task_cfg(&cfg, &rec->config.recorder_task_config,
                            rec->config.encoder_cfg.format == AV_PROCESSOR_FORMAT_ID_OPUS,
                            "audio_recorder_task", "Recorder");

    esp_err_t ret = esp_gmf_task_init(&cfg, &rec->task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create recorder task");
        return ESP_FAIL;
    }
    esp_gmf_pipeline_bind_task(rec->pipe, rec->task);
    esp_gmf_pipeline_loading_jobs(rec->pipe);
    esp_gmf_pipeline_set_event(rec->pipe, recorder_pipeline_event, NULL);
    esp_gmf_task_set_timeout(rec->task, 8000);
    ret = esp_gmf_pipeline_run(rec->pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to run recorder pipeline");

    return ESP_OK;
}

static void recorder_cleanup_on_error(audio_recorder_handle_t rec)
{
    if (rec->pipe) {
        esp_gmf_pipeline_stop(rec->pipe);
        SAFE_CLEANUP(rec->pipe, esp_gmf_pipeline_destroy);
    }
    SAFE_CLEANUP(rec->task, esp_gmf_task_deinit);
    SAFE_CLEANUP(rec->fifo, esp_gmf_fifo_destroy);
#if AV_PROCESSOR_SUPPORT_AFE
    SAFE_CLEANUP(rec->afe_cfg, afe_config_free);
    if (rec->afe_manager) {
        ESP_LOGW(TAG, "AFE manager destroy");
        SAFE_CLEANUP(rec->afe_manager, esp_gmf_afe_manager_destroy);
    }
#endif  /* AV_PROCESSOR_SUPPORT_AFE */
    SAFE_CLEANUP(rec->models, esp_srmodel_deinit);
    rec->frontend = AUDIO_RECORDER_FRONTEND_BYPASS;
}

static esp_err_t recorder_configure_all_encoders(audio_recorder_handle_t rec)
{
    if (!rec->pipe) {
        ESP_LOGE(TAG, "Pipeline not initialized for encoder configuration");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = ESP_OK;
    const av_processor_encoder_config_t *encoder_cfg = &rec->config.encoder_cfg;
    esp_gmf_element_handle_t enc_el = NULL;
    esp_gmf_pipeline_get_el_by_name(rec->pipe, "aud_enc", &enc_el);
    if (!enc_el) {
        ESP_LOGE(TAG, "Failed to get encoder element");
        return ESP_FAIL;
    }
    esp_gmf_info_sound_t info = {0};
    bool processed_pcm = recorder_frontend_uses_processed_pcm(rec->frontend);
    switch (encoder_cfg->format) {
        case AV_PROCESSOR_FORMAT_ID_G711A:
        case AV_PROCESSOR_FORMAT_ID_G711U:
            ESP_LOGI(TAG, "Configuring G711 encoder");
            if (encoder_cfg->params.g711.audio_info.channels != 1 || encoder_cfg->params.g711.audio_info.sample_rate != 8000) {
                ESP_LOGW(TAG, "G711 encoder only supports 1 channel and 8000 sample rate, using default config");
            }
            esp_g711_enc_config_t g711_enc_cfg = ESP_G711_ENC_CONFIG_DEFAULT();
            g711_enc_cfg.channel = encoder_cfg->params.g711.audio_info.channels;
            g711_enc_cfg.sample_rate = encoder_cfg->params.g711.audio_info.sample_rate;
            g711_enc_cfg.frame_duration = encoder_cfg->params.g711.audio_info.frame_duration;
            esp_audio_enc_config_t g711_cfg = {
                .type = encoder_cfg->format == AV_PROCESSOR_FORMAT_ID_G711A ? ESP_AUDIO_TYPE_G711A : ESP_AUDIO_TYPE_G711U,
                .cfg_sz = sizeof(esp_g711_enc_config_t),
                .cfg = &g711_enc_cfg,
            };
            ret = esp_gmf_audio_enc_reconfig(enc_el, &g711_cfg);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to configure G711 encoder");
            if (processed_pcm || audio_manager.config.board_sample_rate != 8000) {
                ret = set_pipeline_dest_rate_if_present(rec->pipe, "aud_rate_cvt", 8000);
                ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set G711 rate converter destination rate");
            }
            if (!processed_pcm && audio_manager.config.board_channels != 1) {
                ret = set_pipeline_dest_channel_if_present(rec->pipe, "aud_ch_cvt", 1);
                ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set G711 channel converter channels");
            }
            if (!processed_pcm && audio_manager.config.board_bits != 16) {
                ret = set_pipeline_dest_bits_if_present(rec->pipe, "aud_bit_cvt", 16);
                ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set G711 bit converter bits");
            }
            esp_gmf_info_sound_t req_info = processed_pcm ? (esp_gmf_info_sound_t) {
                .sample_rates = 16000,
                .channels = 1,
                .bits = 16,
            } : (esp_gmf_info_sound_t) {
                .sample_rates = audio_manager.config.board_sample_rate,
                .channels = audio_manager.config.board_channels,
                .bits = audio_manager.config.board_bits,
            };
            ret = esp_gmf_pipeline_report_info(rec->pipe, ESP_GMF_INFO_SOUND, &req_info, sizeof(req_info));
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to report sound info for G711 encoder");
            break;

        case AV_PROCESSOR_FORMAT_ID_OPUS:
            ESP_LOGI(TAG, "Configuring OPUS encoder");
            esp_opus_enc_config_t opus_enc_cfg = ESP_OPUS_ENC_CONFIG_DEFAULT();
            opus_enc_cfg.channel = encoder_cfg->params.opus.audio_info.channels;
            opus_enc_cfg.sample_rate = encoder_cfg->params.opus.audio_info.sample_rate;
            opus_enc_cfg.frame_duration = convert_opus_enc_frame_duration(encoder_cfg->params.opus.audio_info.frame_duration);
            opus_enc_cfg.enable_vbr = encoder_cfg->params.opus.enable_vbr;
            opus_enc_cfg.bitrate = encoder_cfg->params.opus.bitrate;
            esp_audio_enc_config_t opus_cfg = {
                .type = ESP_AUDIO_TYPE_OPUS,
                .cfg_sz = sizeof(esp_opus_enc_config_t),
                .cfg = &opus_enc_cfg,
            };
            ret = esp_gmf_audio_enc_reconfig(enc_el, &opus_cfg);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to configure OPUS encoder");

            if (!processed_pcm && audio_manager.config.board_sample_rate != encoder_cfg->params.opus.audio_info.sample_rate) {
                ret = set_pipeline_dest_rate_if_present(rec->pipe, "aud_rate_cvt",
                                                        encoder_cfg->params.opus.audio_info.sample_rate);
                ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set OPUS rate converter destination rate");
            }
            if (!processed_pcm && audio_manager.config.board_channels != encoder_cfg->params.opus.audio_info.channels) {
                ret = set_pipeline_dest_channel_if_present(rec->pipe, "aud_ch_cvt",
                                                           encoder_cfg->params.opus.audio_info.channels);
                ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set OPUS channel converter channels");
            }
            if (!processed_pcm && audio_manager.config.board_bits != encoder_cfg->params.opus.audio_info.sample_bits) {
                ret = set_pipeline_dest_bits_if_present(rec->pipe, "aud_bit_cvt",
                                                        encoder_cfg->params.opus.audio_info.sample_bits);
                ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set OPUS bit converter bits");
            }
            info.sample_rates = processed_pcm ? 16000 : audio_manager.config.board_sample_rate;
            info.channels = processed_pcm ? 1 : audio_manager.config.board_channels;
            info.bits = processed_pcm ? 16 : audio_manager.config.board_bits;
            info.format_id = ESP_AUDIO_TYPE_OPUS;
            ret = esp_gmf_pipeline_report_info(rec->pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to report sound info for OPUS encoder");
            break;
        case AV_PROCESSOR_FORMAT_ID_PCM:
        default:
            ESP_LOGI(TAG, "Configuring PCM encoder");
            ret = set_pipeline_dest_rate_if_present(rec->pipe, "aud_rate_cvt",
                                                    encoder_cfg->params.pcm.audio_info.sample_rate);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set PCM rate converter destination rate");
            ret = set_pipeline_dest_channel_if_present(rec->pipe, "aud_ch_cvt",
                                                       encoder_cfg->params.pcm.audio_info.channels);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set PCM channel converter channels");
            ret = set_pipeline_dest_bits_if_present(rec->pipe, "aud_bit_cvt",
                                                    encoder_cfg->params.pcm.audio_info.sample_bits);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set PCM bit converter bits");
            esp_pcm_enc_config_t pcm_enc_cfg = ESP_PCM_ENC_CONFIG_DEFAULT();
            pcm_enc_cfg.sample_rate = encoder_cfg->params.pcm.audio_info.sample_rate;
            pcm_enc_cfg.channel = encoder_cfg->params.pcm.audio_info.channels;
            pcm_enc_cfg.bits_per_sample = encoder_cfg->params.pcm.audio_info.sample_bits;
            pcm_enc_cfg.frame_duration = encoder_cfg->params.pcm.audio_info.frame_duration;

            esp_audio_enc_config_t enc_cfg = {
                .type = ESP_AUDIO_TYPE_PCM,
                .cfg_sz = sizeof(esp_pcm_enc_config_t),
                .cfg = &pcm_enc_cfg,
            };
            ret = esp_gmf_audio_enc_reconfig(enc_el, &enc_cfg);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to configure PCM encoder");

            info.sample_rates = processed_pcm ? 16000 : audio_manager.config.board_sample_rate;
            info.channels = processed_pcm ? 1 : audio_manager.config.board_channels;
            info.bits = processed_pcm ? 16 : audio_manager.config.board_bits;
            ret = esp_gmf_pipeline_report_info(rec->pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to report sound info for PCM encoder");
            break;
    }

    ESP_LOGI(TAG, "All encoders configured successfully");
    return ESP_OK;
}

static int feeder_outport_acquire_write(void *handle, esp_gmf_data_bus_block_t *load, int wanted_size, int block_ticks)
{
    return wanted_size;
}

static int feeder_outport_release_write(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    audio_feeder_handle_t feeder = (audio_feeder_handle_t)handle;
    bool prompt_playing = audio_manager_any_prompt_playing();
    if (prompt_playing) {
        ESP_LOGD(TAG, "Audio prompt is playing, skip write");
        return blk->valid_size;
    }
    if (audio_manager.enable_mixer) {
        if (audio_manager.mixer_feeder != feeder) {
            ESP_LOGD(TAG, "Feeder handle is not bound to mixer, skip write");
            return blk->valid_size;
        }
        esp_audio_render_stream_write(audio_manager.stream[AUDIO_RENDER_STREAM_FEEDER], blk->buf, blk->valid_size);
    } else {
        esp_err_t ret = audio_manager_write_output(blk->buf, blk->valid_size);
        if (ret != ESP_OK) {
            return ESP_FAIL;
        }
    }
    return blk->valid_size;
}

static int feeder_inport_acquire_read(void *handle, esp_gmf_data_bus_block_t *blk, int wanted_size, int block_ticks)
{
    audio_feeder_handle_t fd = (audio_feeder_handle_t)handle;
    esp_gmf_data_bus_block_t _blk = {0};
    if (fd->config.decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_PCM) {
        _blk.buf = blk->buf;
        _blk.buf_length = blk->buf_length;
        int ret = esp_gmf_db_acquire_read(fd->fifo, &_blk, wanted_size, block_ticks);
        if (ret < 0) {
            if (ret == ESP_GMF_IO_ABORT) {
                ESP_LOGW(TAG, "Fifo acquire read aborted, normal operation");
                return ret;
            } else {
                ESP_LOGE(TAG, "Fifo acquire read failed (0x%x)", ret);
            }
            return ESP_FAIL;
        }
        memcpy(blk->buf, _blk.buf, _blk.valid_size);
        blk->valid_size = _blk.valid_size;
        esp_gmf_db_release_read(fd->fifo, &_blk, block_ticks);
        blk->valid_size = _blk.valid_size;
    } else {
        int ret = esp_gmf_db_acquire_read(fd->fifo, &_blk, wanted_size,
                                          block_ticks);
        if (ret < 0) {
            if (ret == ESP_GMF_IO_ABORT) {
                ESP_LOGW(TAG, "Fifo acquire read aborted, normal operation");
                return ret;
            } else {
                ESP_LOGE(TAG, "Fifo acquire read failed (0x%x)", ret);
            }
            return ESP_FAIL;
        }
        memcpy(blk->buf, _blk.buf, _blk.valid_size);
        blk->valid_size = _blk.valid_size;
        esp_gmf_db_release_read(fd->fifo, &_blk, block_ticks);
    }
    return ESP_GMF_ERR_OK;
}

static int feeder_inport_release_read(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    return blk->valid_size;
}

static esp_err_t feeder_configure_all_decoders(audio_feeder_handle_t fd)
{
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_pipeline_get_el_by_name(fd->pipe, "aud_dec", &dec_el);
    if (!dec_el) {
        ESP_LOGE(TAG, "Failed to get decoder element");
        return ESP_FAIL;
    }
    esp_err_t ret = ESP_OK;
    const av_processor_decoder_config_t *decoder_cfg = &fd->config.decoder_cfg;
    esp_gmf_info_sound_t info = {0};
    switch (decoder_cfg->format) {
        case AV_PROCESSOR_FORMAT_ID_PCM:
            ESP_LOGI(TAG, "Configuring PCM decoder");
            info.sample_rates = decoder_cfg->params.pcm.audio_info.sample_rate;
            info.channels = decoder_cfg->params.pcm.audio_info.channels;
            info.bits = decoder_cfg->params.pcm.audio_info.sample_bits;
            info.format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_PCM;
            ret = esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to configure PCM decoder");
            break;

        case AV_PROCESSOR_FORMAT_ID_OPUS:
            ESP_LOGI(TAG, "Configuring OPUS decoder");
            esp_opus_dec_cfg_t opus_dec_cfg = ESP_OPUS_DEC_CONFIG_DEFAULT();
            opus_dec_cfg.frame_duration = convert_opus_dec_frame_duration(decoder_cfg->params.opus.audio_info.frame_duration);
            opus_dec_cfg.channel = decoder_cfg->params.opus.audio_info.channels;
            opus_dec_cfg.sample_rate = decoder_cfg->params.opus.audio_info.sample_rate;

            esp_audio_simple_dec_cfg_t dec_cfg = {
                .dec_type = ESP_AUDIO_TYPE_OPUS,
                .dec_cfg = &opus_dec_cfg,
                .cfg_size = sizeof(esp_opus_dec_cfg_t),
            };
            ret = esp_gmf_audio_dec_reconfig(dec_el, &dec_cfg);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to configure OPUS decoder");

            info.sample_rates = decoder_cfg->params.opus.audio_info.sample_rate;
            info.channels = decoder_cfg->params.opus.audio_info.channels;
            info.bits = 16;
            info.format_id = ESP_FOURCC_OPUS;
            break;

        case AV_PROCESSOR_FORMAT_ID_G711A:
            ESP_LOGI(TAG, "Configuring G711A decoder");
            info.sample_rates = 8000;
            info.channels = 1;
            info.bits = 16;
            info.format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_G711A;
            ret = esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to configure G711A decoder");
            break;

        case AV_PROCESSOR_FORMAT_ID_G711U:
            ESP_LOGI(TAG, "Configuring G711U decoder");
            info.sample_rates = 8000;
            info.channels = 1;
            info.bits = 16;
            info.format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_G711U;
            ret = esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to configure G711U decoder");
            break;

        case AV_PROCESSOR_FORMAT_ID_AAC:  // TODO
            ESP_LOGI(TAG, "Configuring AAC decoder");
            info.sample_rates = 16000;
            info.channels = 1;
            info.bits = 16;
            info.format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
            ret = esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to configure AAC decoder");
            break;

        case AV_PROCESSOR_FORMAT_ID_MP3:
            ESP_LOGI(TAG, "Configuring MP3 decoder");
            info.sample_rates = 16000;
            info.channels = 1;
            info.bits = 16;
            info.format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
            ret = esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to configure MP3 decoder");
            break;

        default:
            ESP_LOGE(TAG, "Unsupported decoder format: " AV_PROCESSOR_FOURCC_FMT, AV_PROCESSOR_FOURCC_ARGS(decoder_cfg->format));
            return ESP_ERR_NOT_SUPPORTED;
    }

    esp_gmf_pipeline_report_info(fd->pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    ret = set_pipeline_dest_rate_if_present(fd->pipe, "aud_rate_cvt", audio_manager.config.board_sample_rate);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set feeder rate converter destination rate");
    ret = set_pipeline_dest_channel_if_present(fd->pipe, "aud_ch_cvt", audio_manager.config.board_channels);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set feeder channel converter channels");
    ret = set_pipeline_dest_bits_if_present(fd->pipe, "aud_bit_cvt", audio_manager.config.board_bits);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to set feeder bit converter bits");
    ESP_LOGI(TAG, "All decoders configured successfully");
    return ESP_OK;
}

static esp_err_t feeder_setup_pipeline_ports(audio_feeder_handle_t fd)
{
    esp_gmf_port_handle_t inport = NEW_ESP_GMF_PORT_IN_BYTE(
        feeder_inport_acquire_read,
        feeder_inport_release_read,
        NULL, fd, AUDIO_BUFFER_SIZE, portMAX_DELAY);

    esp_err_t ret = esp_gmf_pipeline_reg_el_port(fd->pipe, "aud_dec", ESP_GMF_IO_DIR_READER, inport);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to register feeder input port");

    esp_gmf_port_handle_t outport = NEW_ESP_GMF_PORT_OUT_BYTE(
        feeder_outport_acquire_write,
        feeder_outport_release_write,
        NULL, fd, AUDIO_BUFFER_SIZE, portMAX_DELAY);

    ret = esp_gmf_pipeline_reg_el_port(fd->pipe, "aud_rate_cvt", ESP_GMF_IO_DIR_WRITER, outport);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to register feeder output port");

    return ESP_OK;
}

static void feeder_cleanup_on_error(audio_feeder_handle_t fd)
{
    SAFE_CLEANUP(fd->pipe, esp_gmf_pipeline_destroy);
    SAFE_CLEANUP(fd->task, esp_gmf_task_deinit);
    SAFE_CLEANUP(fd->fifo, esp_gmf_db_deinit);
}

static esp_err_t audio_player_release_resources(esp_asp_handle_t *player, esp_gmf_pool_handle_t *pool, const char *module_name)
{
    if (player && *player) {
        esp_err_t err = esp_audio_simple_player_destroy(*player);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to destroy %s player", module_name);
            return ESP_FAIL;
        }
        *player = NULL;
    }
    if (pool && *pool) {
        av_audio_playback_pool_deinit(*pool);
        *pool = NULL;
    }
    return ESP_OK;
}

static void audio_fill_gmf_task_cfg(esp_gmf_task_cfg_t *cfg, const audio_task_config_t *task_cfg,
                                    bool requires_large_stack, const char *task_name, const char *module_name)
{
    esp_gmf_task_cfg_t default_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    *cfg = default_cfg;
    if (task_cfg->task_stack > 0) {
        cfg->thread.stack = task_cfg->task_stack;
    } else {
        cfg->thread.stack = AUDIO_OPUS_PIP_TASK_STACK_SIZE;
    }
    if (requires_large_stack && cfg->thread.stack < AUDIO_OPUS_PIP_TASK_STACK_SIZE) {
        ESP_LOGW(TAG, "%s OPUS pipeline requires larger stack size, using default 40k stack", module_name);
        cfg->thread.stack = AUDIO_OPUS_PIP_TASK_STACK_SIZE;
    }
    cfg->thread.prio = task_cfg->task_prio;
    cfg->thread.core = task_cfg->task_core;
    cfg->thread.stack_in_ext = task_cfg->task_stack_in_ext;
    cfg->name = task_name;
}

esp_err_t audio_manager_init(audio_manager_config_t *config)
{
    ESP_LOGI(TAG, "Initializing audio manager...");
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_FAIL;
    }
    audio_manager.config = *config;

#if CONFIG_AUDIO_PROCESSOR_IMPL_GMF
    esp_err_t media_ret = media_lib_add_default_adapter();
    if (media_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add media lib default adapter: %s", esp_err_to_name(media_ret));
        return media_ret;
    }
#endif

    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();
    esp_audio_enc_register_default();

    memset(audio_manager.players, 0, sizeof(audio_manager.players));
    memset(audio_manager.feeders, 0, sizeof(audio_manager.feeders));
    audio_manager.mixer_playback = NULL;
    audio_manager.mixer_feeder = NULL;
    audio_manager.enable_mixer = false;
    ESP_LOGI(TAG, "Audio manager initialized successfully");
    return ESP_OK;
}

esp_err_t audio_manager_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing audio manager...");
    esp_audio_dec_unregister_default();
    esp_audio_simple_dec_unregister_default();
    esp_audio_enc_unregister_default();
    ESP_LOGI(TAG, "Audio manager deinitialized successfully");
    return ESP_OK;
}

static esp_err_t audio_play_internal(audio_play_handle_t player, const char *url, bool block_until_done)
{
    if (!player || !url) {
        ESP_LOGE(TAG, "Invalid parameters for audio play");
        return ESP_FAIL;
    }
    if (player->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGE(TAG, "Cannot play %s - not opened", audio_play_type_to_str(player->config.type));
        return ESP_FAIL;
    }
    if (player->state == AUDIO_RUN_STATE_PLAYING) {
        ESP_LOGW(TAG, "Audio %s is already playing, stopping current playback",
                 audio_play_type_to_str(player->config.type));
        esp_audio_simple_player_stop(player->player);
    }
    player->state = AUDIO_RUN_STATE_PLAYING;
#if CONFIG_AUDIO_HLS_ENABLE
    player->hls_last_format = 0;
#endif  /* CONFIG_AUDIO_HLS_ENABLE */
    ESP_LOGI(TAG, "Starting %s: %s, state: %d",
             audio_play_type_to_str(player->config.type), url, player->state);
    esp_err_t err = block_until_done ? esp_audio_simple_player_run_to_end(player->player, url, NULL) :
                                       esp_audio_simple_player_run(player->player, url, NULL);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {player->state = AUDIO_RUN_STATE_IDLE; return ESP_FAIL;},
                          "Failed to start audio play");
    if (block_until_done) {
        player->state = AUDIO_RUN_STATE_IDLE;
    }
    return ESP_OK;
}

esp_err_t audio_playback_open(audio_play_config_t *config, audio_play_handle_t *handle)
{
    if (config == NULL || handle == NULL) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_FAIL;
    }
    *handle = NULL;

    audio_play_handle_t player = calloc(1, sizeof(struct audio_play_s));
    if (player == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %s handle", audio_play_type_to_str(config->type));
        return ESP_ERR_NO_MEM;
    }
    player->state = AUDIO_RUN_STATE_CLOSED;
    player->config = *config;

    esp_err_t err = av_audio_playback_pool_init(&player->pool);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init audio %s pool", audio_play_type_to_str(config->type));
        free(player);
        return ESP_FAIL;
    }
    esp_asp_cfg_t cfg = {
        .in.cb = NULL,
        .in.user_ctx = NULL,
        .out.cb = audio_play_out_data_callback,
        .out.user_ctx = player,
        .task_prio = player->config.task_config.task_prio,
        .task_stack = player->config.task_config.task_stack,
        .task_core = player->config.task_config.task_core,
        .task_stack_in_ext = player->config.task_config.task_stack_in_ext,
    };
    err = esp_audio_simple_player_new(&cfg, &player->player);
    if (err != ESP_OK || !player->player) {
        ESP_LOGE(TAG, "Failed to create audio %s player", audio_play_type_to_str(config->type));
        (void)audio_player_release_resources(&player->player, &player->pool, "audio player");
        free(player);
        return ESP_FAIL;
    }
    err = esp_audio_simple_player_set_event(player->player, audio_play_event_callback, player);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set %s event callback", audio_play_type_to_str(config->type));
        (void)audio_player_release_resources(&player->player, &player->pool, "audio player");
        free(player);
        return ESP_FAIL;
    }
#if CONFIG_AUDIO_HLS_ENABLE
    err = audio_play_register_hls_io(player);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register HLS IO for %s", audio_play_type_to_str(config->type));
        (void)audio_player_release_resources(&player->player, &player->pool, "audio player");
        free(player);
        return ESP_FAIL;
    }
#endif  /* CONFIG_AUDIO_HLS_ENABLE */
    player->state = AUDIO_RUN_STATE_IDLE;
    if (!audio_manager_register_player(player)) {
        (void)audio_player_release_resources(&player->player, &player->pool, "audio player");
        free(player);
        return ESP_ERR_NO_MEM;
    }
    *handle = player;
    ESP_LOGI(TAG, "Audio %s opened successfully", audio_play_type_to_str(config->type));
    return ESP_OK;
}

esp_err_t audio_playback_close(audio_play_handle_t player)
{
    audio_play_type_t type;
    if (!player) {
        ESP_LOGW(TAG, "Audio play handle is NULL");
        return ESP_OK;
    }
    type = player->config.type;
    if (player->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGW(TAG, "Audio %s is already closed", audio_play_type_to_str(type));
        free(player);
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Closing audio %s...", audio_play_type_to_str(type));
    if (player->state == AUDIO_RUN_STATE_PLAYING) {
        esp_audio_simple_player_stop(player->player);
    }
    ESP_GMF_RET_ON_NOT_OK(TAG,
                          audio_player_release_resources(&player->player, &player->pool, "audio player"),
                          {return ESP_FAIL;}, "Failed to release audio player resources");
    audio_manager_unregister_player(player);
    free(player);
    ESP_LOGI(TAG, "Audio %s closed successfully", audio_play_type_to_str(type));
    return ESP_OK;
}

esp_err_t audio_playback_play(audio_play_handle_t player, const char *url)
{
    bool block_until_done = player && player->config.block_until_done;
    return audio_play_internal(player, url, block_until_done);
}

esp_err_t audio_playback_pause(audio_play_handle_t player)
{
    if (!player || player->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGE(TAG, "Cannot pause player - not opened");
        return ESP_FAIL;
    }
    if (audio_play_is_prompt(player)) {
        ESP_LOGE(TAG, "Prompt player does not support pause");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (player->state == AUDIO_RUN_STATE_PAUSED) {
        ESP_LOGW(TAG, "Audio player is already paused");
        return ESP_OK;
    }
    if (player->state != AUDIO_RUN_STATE_PLAYING) {
        ESP_LOGE(TAG, "Cannot pause player - current state: %d", player->state);
        return ESP_FAIL;
    }
    esp_err_t err = esp_audio_simple_player_pause(player->player);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {return ESP_FAIL;}, "Failed to pause player");
    player->state = AUDIO_RUN_STATE_PAUSED;
    return ESP_OK;
}

esp_err_t audio_playback_resume(audio_play_handle_t player)
{
    if (!player || player->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGE(TAG, "Cannot resume player - not opened");
        return ESP_FAIL;
    }
    if (audio_play_is_prompt(player)) {
        ESP_LOGE(TAG, "Prompt player does not support resume");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (player->state == AUDIO_RUN_STATE_PLAYING) {
        ESP_LOGW(TAG, "Audio player is already playing");
        return ESP_OK;
    }
    if (player->state != AUDIO_RUN_STATE_PAUSED) {
        ESP_LOGE(TAG, "Cannot resume player - current state: %d", player->state);
        return ESP_FAIL;
    }
    esp_err_t err = esp_audio_simple_player_resume(player->player);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {return ESP_FAIL;}, "Failed to resume player");
    player->state = AUDIO_RUN_STATE_PLAYING;
    return ESP_OK;
}

esp_err_t audio_playback_stop(audio_play_handle_t player)
{
    if (!player || player->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGE(TAG, "Cannot stop player - not opened");
        return ESP_FAIL;
    }
    if (player->state == AUDIO_RUN_STATE_IDLE) {
        ESP_LOGW(TAG, "Audio player is already idle");
        return ESP_OK;
    }
    esp_err_t err = esp_audio_simple_player_stop(player->player);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {return ESP_FAIL;}, "Failed to stop player");
    player->state = AUDIO_RUN_STATE_IDLE;
    return ESP_OK;
}

esp_err_t audio_recorder_open(audio_recorder_config_t *config, audio_recorder_handle_t *handle)
{
    ESP_LOGI(TAG, "Opening audio recorder...");

    if (config == NULL || handle == NULL) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_FAIL;
    }
    *handle = NULL;

    audio_recorder_handle_t rec = calloc(1, sizeof(struct audio_recorder_s));
    if (rec == NULL) {
        ESP_LOGE(TAG, "Failed to allocate recorder handle");
        return ESP_ERR_NO_MEM;
    }
    rec->state = AUDIO_RUN_STATE_CLOSED;
    memcpy(&rec->config, config, sizeof(audio_recorder_config_t));

    esp_err_t err = esp_gmf_fifo_create(5, 1, &rec->fifo);
    if (err != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create FIFO, err: %d", err);
        free(rec);
        return ESP_FAIL;
    }
    av_audio_recorder_pool_init(&rec->pool, &rec->config.encoder_cfg);
    ESP_GMF_POOL_SHOW_ITEMS(rec->pool);
    const char **elements;
    int element_count = 0;
    const char *input_name = NULL, *output_name = NULL;
    err = recorder_select_frontend(rec, &rec->frontend);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {goto cleanup_and_exit;}, "Failed to select recorder frontend");
    err = recorder_validate_frontend(rec->frontend);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {goto cleanup_and_exit;}, "Recorder frontend input format is not supported");
    ESP_LOGI(TAG, "Selected recorder frontend: %s", audio_recorder_frontend_to_str(rec->frontend));

    if (rec->frontend == AUDIO_RECORDER_FRONTEND_AFE) {
#if AV_PROCESSOR_SUPPORT_AFE
        err = recorder_setup_afe_config(rec);
        ESP_GMF_RET_ON_NOT_OK(TAG, err, {goto cleanup_and_exit;}, "Failed to setup AFE config");
#else
        err = ESP_ERR_NOT_SUPPORTED;
        ESP_GMF_RET_ON_NOT_OK(TAG, err, {goto cleanup_and_exit;}, "AFE frontend is not compiled in");
#endif  /* AV_PROCESSOR_SUPPORT_AFE */
    } else if (rec->frontend == AUDIO_RECORDER_FRONTEND_WN) {
        err = recorder_setup_wn_config(rec);
        ESP_GMF_RET_ON_NOT_OK(TAG, err, {goto cleanup_and_exit;}, "Failed to setup WN config");
    }

    recorder_get_pipeline_config(rec->frontend, &rec->config.encoder_cfg,
                                 &elements, &element_count, &input_name, &output_name);

    err = esp_gmf_pool_new_pipeline(rec->pool, NULL, elements, element_count, NULL, &rec->pipe);
    if (err != ESP_OK || rec->pipe == NULL) {
        ESP_LOGE(TAG, "Failed to create recorder pipeline: %02x", err);
        goto cleanup_and_exit;
    }
    err = recorder_configure_all_encoders(rec);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {goto cleanup_and_exit;}, "Failed to configure encoders");

    err = recorder_setup_pipeline_ports(rec, input_name, output_name);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {goto cleanup_and_exit;}, "Failed to setup pipeline ports");

    err = recorder_setup_task_and_run(rec);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {goto cleanup_and_exit;}, "Failed to setup task and run");

    rec->state = AUDIO_RUN_STATE_PLAYING;
    *handle = rec;

    ESP_LOGI(TAG, "Audio recorder opened successfully");
    return ESP_OK;

cleanup_and_exit:
    ESP_LOGE(TAG, "Failed to open audio recorder, cleaning up...");
    recorder_cleanup_on_error(rec);
    free(rec);
    return ESP_FAIL;
}

esp_err_t audio_recorder_pause(audio_recorder_handle_t rec)
{
    if (!rec || rec->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGE(TAG, "Cannot pause recorder - not opened");
        return ESP_FAIL;
    }
    if (rec->state == AUDIO_RUN_STATE_PAUSED) {
        ESP_LOGW(TAG, "Audio recorder is already paused");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Pausing audio recorder...");
    esp_err_t ret = esp_gmf_pipeline_pause(rec->pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to pause recorder pipeline");
    rec->state = AUDIO_RUN_STATE_PAUSED;
    ESP_LOGI(TAG, "Audio recorder paused successfully");
    return ESP_OK;
}

esp_err_t audio_recorder_resume(audio_recorder_handle_t rec)
{
    if (!rec || rec->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGE(TAG, "Cannot resume recorder - not opened");
        return ESP_FAIL;
    }
    if (rec->state == AUDIO_RUN_STATE_PLAYING) {
        ESP_LOGW(TAG, "Audio recorder is already running");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Resuming audio recorder...");
    esp_err_t ret = esp_gmf_pipeline_resume(rec->pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to resume recorder pipeline");
    rec->state = AUDIO_RUN_STATE_PLAYING;
    ESP_LOGI(TAG, "Audio recorder resumed successfully");
    return ESP_OK;
}

esp_err_t audio_recorder_get_afe_manager_handle(audio_recorder_handle_t rec, esp_gmf_afe_manager_handle_t *afe_manager_handle)
{
#if AV_PROCESSOR_SUPPORT_AFE
    if (!rec || !afe_manager_handle) {
        ESP_LOGE(TAG, "Invalid parameters for recorder get origin AFE handle");
        return ESP_FAIL;
    }
    if (rec->frontend != AUDIO_RECORDER_FRONTEND_AFE) {
        ESP_LOGE(TAG, "Current recorder frontend is %s, no AFE manager is available",
                 audio_recorder_frontend_to_str(rec->frontend));
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!rec->afe_manager) {
        ESP_LOGE(TAG, "Audio recorder AFE manager not initialized");
        return ESP_FAIL;
    }
    *afe_manager_handle = rec->afe_manager;
    return ESP_OK;
#else
    ESP_LOGE(TAG, "Audio AFE is not compiled in");
    return ESP_FAIL;
#endif  /* AV_PROCESSOR_SUPPORT_AFE */
}

esp_err_t audio_recorder_get_afe_element_handle(audio_recorder_handle_t rec, esp_gmf_element_handle_t *afe_element_handle)
{
    if (!rec || !afe_element_handle) {
        ESP_LOGE(TAG, "Invalid parameters for recorder get AFE element handle");
        return ESP_FAIL;
    }
    if (rec->frontend != AUDIO_RECORDER_FRONTEND_AFE) {
        ESP_LOGE(TAG, "Current recorder frontend is %s, no AFE element is available",
                 audio_recorder_frontend_to_str(rec->frontend));
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_gmf_pipeline_get_el_by_name(rec->pipe, "ai_afe", afe_element_handle);
    if (!(*afe_element_handle)) {
        ESP_LOGE(TAG, "Failed to get AFE element handle");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t audio_recorder_close(audio_recorder_handle_t rec)
{
    if (!rec) {
        ESP_LOGW(TAG, "Audio recorder handle is NULL");
        return ESP_OK;
    }
    if (rec->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGW(TAG, "Audio recorder is already closed");
        free(rec);
        return ESP_OK;
    }
    UBaseType_t basic_priority = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, 23);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Closing audio recorder...");
    rec->state = AUDIO_RUN_STATE_CLOSED;
    if (rec->pipe) {
        if (rec->fifo) {
            esp_gmf_fifo_abort(rec->fifo);
        }
        esp_gmf_pipeline_stop(rec->pipe);
    }
    SAFE_CLEANUP(rec->pipe, esp_gmf_pipeline_destroy);
    SAFE_CLEANUP(rec->task, esp_gmf_task_deinit);
    SAFE_CLEANUP(rec->fifo, esp_gmf_fifo_destroy);
#if AV_PROCESSOR_SUPPORT_AFE
    SAFE_CLEANUP(rec->afe_cfg, afe_config_free);
    SAFE_CLEANUP(rec->afe_manager, esp_gmf_afe_manager_destroy);
#endif  /* AV_PROCESSOR_SUPPORT_AFE */
    SAFE_CLEANUP(rec->models, esp_srmodel_deinit);
    SAFE_CLEANUP(rec->pool, av_audio_recorder_pool_deinit);

    vTaskPrioritySet(NULL, basic_priority);

    free(rec);
    ESP_LOGI(TAG, "Audio recorder closed successfully");
    return ESP_OK;
}

esp_err_t audio_recorder_read_data(audio_recorder_handle_t rec, uint8_t *data, int data_size)
{
    if (!rec || !data || data_size <= 0) {
        ESP_LOGE(TAG, "Invalid parameters for recorder read");
        return ESP_FAIL;
    }
    if (rec->state == AUDIO_RUN_STATE_CLOSED || !rec->fifo) {
        ESP_LOGW(TAG, "Recorder not available for reading, state: %d, fifo: %p", rec->state, rec->fifo);
        return ESP_FAIL;
    }
    esp_gmf_data_bus_block_t blk = {0};
    int ret = esp_gmf_fifo_acquire_read(rec->fifo, &blk, data_size, portMAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to acquire read from recorder FIFO (%d)", ret);
        return ret;
    }
    int bytes_to_copy = blk.valid_size;
    if (bytes_to_copy > data_size) {
        ESP_LOGI(TAG, "Read data size is larger than expected, valid size: %d, expected: %d", bytes_to_copy, data_size);
        bytes_to_copy = data_size;
    }
    memcpy(data, blk.buf, bytes_to_copy);
    esp_gmf_fifo_release_read(rec->fifo, &blk, portMAX_DELAY);

#if CONFIG_MEDIA_DUMP_ENABLE && CONFIG_MEDIA_DUMP_AUDIO_AFTER_AEC
    media_dump_feed(NULL, (uint8_t *)blk.buf, blk.valid_size);
#endif  /* CONFIG_MEDIA_DUMP_ENABLE && CONFIG_MEDIA_DUMP_AUDIO_AFTER_AEC */
    return bytes_to_copy;
}
esp_err_t audio_feeder_feed_data(audio_feeder_handle_t fd, uint8_t *data, int data_size)
{
    if (!fd || !data || data_size <= 0) {
        ESP_LOGE(TAG, "Invalid parameters for feeder feed");
        return ESP_FAIL;
    }
    if (fd->state == AUDIO_RUN_STATE_CLOSED || !fd->fifo) {
        ESP_LOGW(TAG, "Feeder not available for feeding");
        return ESP_FAIL;
    }
    esp_gmf_data_bus_block_t blk = {0};

    if (fd->config.decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_PCM) {
        /* LOCAL FIX (2026-11-01): 播放侧诊断——每 16 次 feed 前打一次 HAL
         * ringbuf 水位。低水位(≈0)= I2S 消费快于喂入→输出缺口→听感断续;
         * 高水位(≈32768)= 消费正常/堆积。v15 deq 只测 decoder 队列,漏了
         * HAL ringbuf 真实水位,本次直接测量 I2S 输入侧。 */
        static uint32_t s_feed_log_count = 0;
        if ((++s_feed_log_count % 16) == 0) {
            uint32_t used = 0;
            esp_gmf_db_get_filled_size(fd->fifo, &used);
            ESP_LOGW(TAG, "AUD_FEED: ringbuf used=%u", (unsigned)used);
        }
        int ret = esp_gmf_db_acquire_write(fd->fifo, &blk, data_size, portMAX_DELAY);
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to acquire write to feeder FIFO (0x%x)", ret);
            return ESP_FAIL;
        }
        blk.buf = (uint8_t *)data;
        blk.valid_size = data_size;
        ret = esp_gmf_db_release_write(fd->fifo, &blk, portMAX_DELAY);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to release write to feeder FIFO");
    } else {
        int ret = esp_gmf_db_acquire_write(fd->fifo, &blk, data_size, portMAX_DELAY);
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to acquire write to feeder FIFO (0x%x)", ret);
            return ESP_FAIL;
        }
        int bytes_to_copy = (data_size < blk.buf_length) ? data_size : blk.buf_length;
        memcpy(blk.buf, data, bytes_to_copy);
        blk.valid_size = bytes_to_copy;

        ret = esp_gmf_db_release_write(fd->fifo, &blk, portMAX_DELAY);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to release write to feeder FIFO");
    }
    return ESP_OK;
}

esp_err_t audio_feeder_open(audio_feeder_config_t *config, audio_feeder_handle_t *handle)
{
    ESP_LOGI(TAG, "Opening audio feeder...");
    if (config == NULL || handle == NULL) {
        ESP_LOGE(TAG, "Invalid decoder configuration");
        return ESP_FAIL;
    }
    *handle = NULL;

    audio_feeder_handle_t fd = calloc(1, sizeof(struct audio_feeder_s));
    if (fd == NULL) {
        ESP_LOGE(TAG, "Failed to allocate feeder handle");
        return ESP_ERR_NO_MEM;
    }
    fd->state = AUDIO_RUN_STATE_CLOSED;
    memcpy(&fd->config, config, sizeof(audio_feeder_config_t));

    esp_err_t err;
    if (fd->config.decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_PCM) {
        err = esp_gmf_db_new_ringbuf(1, AUDIO_RINGBUFFER_SIZE, &fd->fifo);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create feeder ringbuffer");
            free(fd);
            return ESP_FAIL;
        }
    } else {
        err = esp_gmf_db_new_fifo(AUDIO_FIFO_NUM, 1, &fd->fifo);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create feeder FIFO");
            free(fd);
            return ESP_FAIL;
        }
    }
    const char *elements[] = {"aud_dec", "aud_ch_cvt", "aud_bit_cvt", "aud_rate_cvt"};
    av_audio_feeder_pool_init(&fd->pool, &fd->config.decoder_cfg);
    ESP_GMF_POOL_SHOW_ITEMS(fd->pool);
    err = esp_gmf_pool_new_pipeline(fd->pool, NULL, elements,
                                    sizeof(elements) / sizeof(char *), NULL, &fd->pipe);
    if (err != ESP_OK || !fd->pipe) {
        ESP_LOGE(TAG, "Failed to create feeder pipeline");
        goto cleanup_and_exit;
    }
    err = feeder_configure_all_decoders(fd);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {goto cleanup_and_exit;}, "Failed to configure decoders");

    err = feeder_setup_pipeline_ports(fd);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {goto cleanup_and_exit;}, "Failed to setup pipeline ports");

    esp_gmf_task_cfg_t cfg = {0};
    audio_fill_gmf_task_cfg(&cfg, &fd->config.feeder_task_config,
                            fd->config.decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_OPUS,
                            "audio_feeder_task", "Feeder");

    err = esp_gmf_task_init(&cfg, &fd->task);
    ESP_GMF_RET_ON_NOT_OK(TAG, err, {goto cleanup_and_exit;}, "Failed to create feeder task");

    esp_gmf_pipeline_bind_task(fd->pipe, fd->task);
    esp_gmf_pipeline_loading_jobs(fd->pipe);

    fd->state = AUDIO_RUN_STATE_IDLE;
    if (!audio_manager_register_feeder(fd)) {
        goto cleanup_and_exit;
    }
    *handle = fd;
    ESP_LOGI(TAG, "Audio feeder opened successfully");
    return ESP_OK;

cleanup_and_exit:
    ESP_LOGE(TAG, "Failed to open audio feeder, cleaning up...");
    feeder_cleanup_on_error(fd);
    free(fd);
    return ESP_FAIL;
}

esp_err_t audio_feeder_close(audio_feeder_handle_t fd)
{
    if (!fd) {
        ESP_LOGW(TAG, "Audio feeder handle is NULL");
        return ESP_OK;
    }
    if (fd->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGW(TAG, "Audio feeder is already closed");
        free(fd);
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Closing audio feeder...");
    if (fd->state == AUDIO_RUN_STATE_PLAYING) {
        audio_feeder_stop(fd);
    }
    SAFE_CLEANUP(fd->pipe, esp_gmf_pipeline_destroy);
    SAFE_CLEANUP(fd->task, esp_gmf_task_deinit);
    SAFE_CLEANUP(fd->fifo, esp_gmf_db_deinit);
    SAFE_CLEANUP(fd->pool, av_audio_feeder_pool_deinit);
    audio_manager_unregister_feeder(fd);
    free(fd);
    ESP_LOGI(TAG, "Audio feeder closed successfully");
    return ESP_OK;
}

esp_err_t audio_feeder_start(audio_feeder_handle_t fd)
{
    if (!fd || fd->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGE(TAG, "Cannot start feeder - not opened");
        return ESP_FAIL;
    }
    if (fd->state == AUDIO_RUN_STATE_PLAYING) {
        ESP_LOGW(TAG, "Audio feeder is already running");
        return ESP_OK;
    }
    esp_err_t ret = esp_gmf_pipeline_run(fd->pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to start feeder pipeline");

    fd->state = AUDIO_RUN_STATE_PLAYING;
    ESP_LOGI(TAG, "Audio feeder started successfully");
    return ESP_OK;
}

esp_err_t audio_feeder_stop(audio_feeder_handle_t fd)
{
    if (!fd || fd->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGE(TAG, "Cannot stop feeder - not opened");
        return ESP_FAIL;
    }
    if (fd->state == AUDIO_RUN_STATE_IDLE) {
        ESP_LOGW(TAG, "Audio feeder is already stopped");
        return ESP_OK;
    }
    if (fd->fifo) {
        esp_gmf_db_abort(fd->fifo);
    }
    ESP_LOGI(TAG, "Stopping audio feeder...");
    esp_err_t ret = esp_gmf_pipeline_stop(fd->pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ESP_FAIL;}, "Failed to stop feeder pipeline");

    fd->state = AUDIO_RUN_STATE_IDLE;
    ESP_LOGI(TAG, "Audio feeder stopped successfully");
    return ESP_OK;
}

esp_err_t audio_processor_mixer_open(audio_play_handle_t pb, audio_feeder_handle_t fd)
{
    if (!pb || !fd) {
        ESP_LOGE(TAG, "Invalid playback or feeder handle for mixer open");
        return ESP_ERR_INVALID_ARG;
    }
    if (audio_manager.enable_mixer) {
        ESP_LOGW(TAG, "Audio mixer already enabled");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Initializing audio render...");
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = AUDIO_RENDER_STREAM_TYPE_MAX,
        .out_sample_info = {
            .sample_rate = audio_manager.config.board_sample_rate,
            .bits_per_sample = audio_manager.config.board_bits,
            .channel = audio_manager.config.board_channels,
        },
        .out_writer = audio_render_write_hdlr,
    };
    esp_err_t ret = esp_audio_render_create(&cfg, &audio_manager.render_handle);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create audio render");

    esp_audio_render_stream_get(audio_manager.render_handle, ESP_AUDIO_RENDER_FIRST_STREAM, &audio_manager.stream[AUDIO_RENDER_STREAM_FEEDER]);
    esp_audio_render_stream_get(audio_manager.render_handle, ESP_AUDIO_RENDER_SECOND_STREAM, &audio_manager.stream[AUDIO_RENDER_STREAM_PLAYBACK]);
    audio_manager.mixer_feeder = fd;
    audio_manager.mixer_playback = pb;

    esp_audio_render_mixer_gain_t feeder_mixer_gain = {0};
    feeder_mixer_gain.initial_gain = fd->config.feeder_mixer_gain.initial_gain;
    feeder_mixer_gain.target_gain = fd->config.feeder_mixer_gain.target_gain;
    feeder_mixer_gain.transition_time = fd->config.feeder_mixer_gain.transition_time;
    esp_audio_render_stream_set_mixer_gain(audio_manager.stream[AUDIO_RENDER_STREAM_FEEDER], &feeder_mixer_gain);

    esp_audio_render_mixer_gain_t playback_mixer_gain = {0};
    playback_mixer_gain.initial_gain = pb->config.mixer_gain.initial_gain;
    playback_mixer_gain.target_gain = pb->config.mixer_gain.target_gain;
    playback_mixer_gain.transition_time = pb->config.mixer_gain.transition_time;
    esp_audio_render_stream_set_mixer_gain(audio_manager.stream[AUDIO_RENDER_STREAM_PLAYBACK], &playback_mixer_gain);

    esp_audio_render_sample_info_t render_sample_info = {
        .sample_rate = audio_manager.config.board_sample_rate,
        .bits_per_sample = audio_manager.config.board_bits,
        .channel = audio_manager.config.board_channels,
    };
    esp_audio_render_set_out_sample_info(audio_manager.render_handle, &render_sample_info);
    esp_err_t err = esp_audio_render_stream_open(audio_manager.stream[AUDIO_RENDER_STREAM_PLAYBACK], &render_sample_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open audio render stream");
        goto cleanup_and_exit;
    }
    err = esp_audio_render_stream_open(audio_manager.stream[AUDIO_RENDER_STREAM_FEEDER], &render_sample_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open audio render stream");
        goto cleanup_and_exit;
    }

    audio_manager.enable_mixer = true;

    return ESP_OK;

cleanup_and_exit:
    for (int i = 0; i < AUDIO_RENDER_STREAM_TYPE_MAX; i++) {
        if (audio_manager.stream[i]) {
            esp_audio_render_stream_close(audio_manager.stream[i]);
            audio_manager.stream[i] = NULL;
        }
    }
    SAFE_CLEANUP(audio_manager.render_handle, esp_audio_render_destroy);
    audio_manager.mixer_playback = NULL;
    audio_manager.mixer_feeder = NULL;
    audio_manager.enable_mixer = false;
    return err;
}

esp_err_t audio_processor_mixer_close(void)
{
    if (!audio_manager.enable_mixer) {
        ESP_LOGE(TAG, "Audio mixer not enabled");
        return ESP_FAIL;
    }
    for (int i = 0; i < AUDIO_RENDER_STREAM_TYPE_MAX; i++) {
        if (audio_manager.stream[i]) {
            esp_audio_render_stream_close(audio_manager.stream[i]);
            audio_manager.stream[i] = NULL;
        }
    }
    SAFE_CLEANUP(audio_manager.render_handle, esp_audio_render_destroy);
    audio_manager.mixer_playback = NULL;
    audio_manager.mixer_feeder = NULL;
    audio_manager.enable_mixer = false;
    return ESP_OK;
}

esp_err_t audio_processor_ramp_control(audio_mixer_focus_mode_t mode)
{
    if (!audio_manager.enable_mixer || !audio_manager.render_handle) {
        ESP_LOGE(TAG, "Audio render not initialized or mixer not enabled, enable_mixer: %d, render_handle: %p", audio_manager.enable_mixer, audio_manager.render_handle);
        return ESP_ERR_INVALID_STATE;
    }
    if (!audio_manager.stream[AUDIO_RENDER_STREAM_FEEDER] || !audio_manager.stream[AUDIO_RENDER_STREAM_PLAYBACK]) {
        ESP_LOGE(TAG, "Audio render streams not available");
        return ESP_ERR_INVALID_STATE;
    }
    switch (mode) {
        case AUDIO_MIXER_FOCUS_FEEDER:
            esp_audio_render_stream_set_fade(audio_manager.stream[AUDIO_RENDER_STREAM_FEEDER], true);
            esp_audio_render_stream_set_fade(audio_manager.stream[AUDIO_RENDER_STREAM_PLAYBACK], false);
            break;
        case AUDIO_MIXER_FOCUS_PLAYBACK:
            esp_audio_render_stream_set_fade(audio_manager.stream[AUDIO_RENDER_STREAM_PLAYBACK], true);
            esp_audio_render_stream_set_fade(audio_manager.stream[AUDIO_RENDER_STREAM_FEEDER], false);
            break;
        case AUDIO_MIXER_FOCUS_BALANCED:
            esp_audio_render_stream_set_fade(audio_manager.stream[AUDIO_RENDER_STREAM_FEEDER], true);
            esp_audio_render_stream_set_fade(audio_manager.stream[AUDIO_RENDER_STREAM_PLAYBACK], true);
            break;
        default:
            ESP_LOGE(TAG, "Invalid focus mode: %d", mode);
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
