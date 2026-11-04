/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "esp_gmf_pool.h"
#include "esp_audio_simple_player_advance.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_enc_default.h"
#include "esp_audio_simple_dec_default.h"

#include "decoder/esp_audio_dec.h"
#include "decoder/impl/esp_g711_dec.h"
#include "decoder/impl/esp_opus_dec.h"
#include "encoder/esp_audio_enc.h"
#include "encoder/impl/esp_g711_enc.h"
#include "encoder/impl/esp_opus_enc.h"
#include "esp_ae_bit_cvt.h"
#include "esp_ae_ch_cvt.h"
#include "esp_ae_rate_cvt.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

#include "av_pipeline_pool.h"
#include "audio_processor.h"

static const char *TAG = "AUDIO_PROCESSOR";

#define AUDIO_LIGHT_PACKET_MAX           (1024)
#define AUDIO_LIGHT_REC_QUEUE_LEN        (2)
#define AUDIO_LIGHT_FEED_QUEUE_LEN       (2)
#define AUDIO_LIGHT_WORKER_STACK_DEFAULT (2048 * 12)
#define AUDIO_LIGHT_WORKER_PRIO_DEFAULT  (5)
#define AUDIO_LIGHT_POLL_MS              (10)
#define AUDIO_LIGHT_CLOSE_WAIT_MS        (5)
#define AUDIO_LIGHT_WN_RATE              (16000)
#define AUDIO_LIGHT_WN_BITS              (16)
#define AUDIO_LIGHT_WN_CH                (1)

#define SAFE_FREE(ptr)           \
    do {                         \
        if ((ptr) != NULL) {     \
            free(ptr);           \
            (ptr) = NULL;        \
        }                        \
    } while (0)

enum audio_play_state_e {
    AUDIO_RUN_STATE_IDLE,
    AUDIO_RUN_STATE_PLAYING,
    AUDIO_RUN_STATE_CLOSED,
    AUDIO_RUN_STATE_PAUSED,
};

typedef struct {
    size_t   len;
    uint8_t  data[];
} audio_light_packet_t;

typedef struct {
    bool                      bypass;
    uint32_t                  src_rate;
    uint32_t                  dst_rate;
    uint8_t                   src_bits;
    uint8_t                   dst_bits;
    uint8_t                   src_ch;
    uint8_t                   dst_ch;
    uint32_t                  input_sample_capacity;
    uint32_t                  max_out_samples;
    esp_ae_ch_cvt_handle_t    ch_cvt;
    esp_ae_bit_cvt_handle_t   bit_cvt;
    esp_ae_rate_cvt_handle_t  rate_cvt;
    uint8_t                  *ch_buf;
    size_t                    ch_buf_size;
    uint8_t                  *bit_buf;
    size_t                    bit_buf_size;
    uint8_t                  *rate_buf;
    size_t                    rate_buf_size;
} audio_convert_ctx_t;

struct audio_play_s {
    esp_asp_handle_t        player;
    enum audio_play_state_e state;
    esp_gmf_pool_handle_t   pool;
    audio_play_config_t     config;
};

struct audio_recorder_s {
    enum audio_play_state_e    state;
    audio_recorder_config_t    config;
    QueueHandle_t              out_queue;
    audio_light_packet_t      *pending_pkt;
    size_t                     pending_offset;
    uint8_t                   *input_buf;
    size_t                     input_buf_size;
    audio_convert_ctx_t        to_wn_cvt;
    audio_convert_ctx_t        to_enc_cvt;
    uint8_t                   *wn_cache;
    size_t                     wn_cache_len;
    size_t                     wn_cache_cap;
    uint8_t                   *enc_pcm_cache;
    size_t                     enc_pcm_len;
    size_t                     enc_pcm_cap;
    uint8_t                   *enc_out_buf;
    size_t                     enc_out_buf_size;
    int                        enc_in_bytes;
    int                        enc_out_bytes;
    esp_audio_enc_handle_t     encoder;
    srmodel_list_t            *models;
    const esp_wn_iface_t      *wn_iface;
    model_iface_data_t        *wn_model;
    audio_recorder_wn_event_t  wn_event;
    int                        wn_frame_samples;
    size_t                     wn_frame_bytes;
};

struct audio_feeder_s {
    enum audio_play_state_e  state;
    audio_feeder_config_t    config;
    QueueHandle_t            in_queue;
    esp_audio_dec_handle_t   decoder;
    audio_convert_ctx_t      to_board_cvt;
    uint8_t                 *dec_out_buf;
    size_t                   dec_out_buf_size;
};

typedef struct {
    audio_manager_config_t  config;
    SemaphoreHandle_t       lock;
    TaskHandle_t            worker_task;
    bool                    worker_stop;
    struct audio_play_s    *player;
    struct audio_recorder_s *recorder;
    struct audio_feeder_s   *feeder;
    bool                    recorder_busy;
    bool                    feeder_busy;
} audio_manager_t;

static audio_manager_t audio_manager;

static int audio_bytes_per_sample(int bits)
{
    return bits / 8;
}

static int audio_bytes_per_frame(int bits, int channels)
{
    return audio_bytes_per_sample(bits) * channels;
}

static audio_light_packet_t *audio_light_packet_alloc(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || len > AUDIO_LIGHT_PACKET_MAX) {
        ESP_LOGE(TAG, "Invalid packet alloc");
        return NULL;
    }
    audio_light_packet_t *pkt = calloc(1, sizeof(audio_light_packet_t) + len);
    if (pkt == NULL) {
        ESP_LOGE(TAG, "No memory for packet");
        return NULL;
    }
    pkt->len = len;
    memcpy(pkt->data, data, len);
    return pkt;
}

static void audio_light_packet_free(audio_light_packet_t *pkt)
{
    SAFE_FREE(pkt);
}

static void audio_light_packet_queue_drain(QueueHandle_t queue)
{
    if (queue == NULL) {
        return;
    }
    audio_light_packet_t *pkt = NULL;
    while (xQueueReceive(queue, &pkt, 0) == pdTRUE) {
        audio_light_packet_free(pkt);
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

static const char *audio_play_type_to_str(audio_play_type_t type)
{
    switch (type) {
        case AUDIO_PLAY_TYPE_PROMPT:
            return "prompt";
        case AUDIO_PLAY_TYPE_PLAYBACK:
        default:
            return "playback";
    }
}

static bool audio_manager_any_prompt_playing(void)
{
    audio_play_handle_t player = audio_manager.player;
    return player && audio_play_is_prompt(player) && player->state == AUDIO_RUN_STATE_PLAYING;
}

static esp_err_t audio_manager_write_output(uint8_t *data, int data_size)
{
    if (audio_manager.config.play_io.write_cb) {
        return audio_manager.config.play_io.write_cb(data, data_size, audio_manager.config.play_io.write_ctx);
    }
    ESP_LOGE(TAG, "No playback output configured");
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t audio_manager_read_input(uint8_t *data, int data_size)
{
    if (audio_manager.config.rec_io.read_cb) {
        return audio_manager.config.rec_io.read_cb(data, data_size, audio_manager.config.rec_io.read_ctx);
    }
    ESP_LOGE(TAG, "No record input configured");
    return ESP_ERR_INVALID_STATE;
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

static int audio_play_out_data_callback(uint8_t *data, int data_size, void *ctx)
{
    audio_play_handle_t player = (audio_play_handle_t)ctx;
    if (audio_play_is_prompt(player)) {
        return audio_manager_write_output(data, data_size);
    }
    if (player->state != AUDIO_RUN_STATE_PLAYING || audio_manager_any_prompt_playing()) {
        return ESP_OK;
    }
    return audio_manager_write_output(data, data_size);
}

static int audio_play_event_callback(esp_asp_event_pkt_t *event, void *ctx)
{
    audio_play_handle_t player = (audio_play_handle_t)ctx;
    if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
        esp_asp_music_info_t info = {0};
        memcpy(&info, event->payload, event->payload_size);
        ESP_LOGI(TAG, "Player info rate:%d channels:%d bits:%d", info.sample_rate, info.channels, info.bits);
    } else if (event->type == ESP_ASP_EVENT_TYPE_STATE) {
        esp_asp_state_t st = 0;
        memcpy(&st, event->payload, event->payload_size);
        if (st == ESP_ASP_STATE_STOPPED || st == ESP_ASP_STATE_FINISHED || st == ESP_ASP_STATE_ERROR) {
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

static esp_err_t audio_convert_reserve(audio_convert_ctx_t *ctx, uint32_t in_samples)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ctx->bypass || in_samples <= ctx->input_sample_capacity) {
        return ESP_OK;
    }

    if (ctx->ch_cvt) {
        size_t new_size = (size_t)in_samples * ctx->dst_ch * audio_bytes_per_sample(ctx->src_bits);
        if (new_size > ctx->ch_buf_size) {
            uint8_t *buf = realloc(ctx->ch_buf, new_size);
            ESP_RETURN_ON_FALSE(buf != NULL, ESP_ERR_NO_MEM, TAG, "No memory for channel convert buffer");
            ctx->ch_buf = buf;
            ctx->ch_buf_size = new_size;
        }
    }
    if (ctx->bit_cvt) {
        size_t new_size = (size_t)in_samples * ctx->dst_ch * audio_bytes_per_sample(ctx->dst_bits);
        if (new_size > ctx->bit_buf_size) {
            uint8_t *buf = realloc(ctx->bit_buf, new_size);
            ESP_RETURN_ON_FALSE(buf != NULL, ESP_ERR_NO_MEM, TAG, "No memory for bit convert buffer");
            ctx->bit_buf = buf;
            ctx->bit_buf_size = new_size;
        }
    }
    if (ctx->rate_cvt) {
        uint32_t max_out = 0;
        esp_ae_err_t ae_ret = esp_ae_rate_cvt_get_max_out_sample_num(ctx->rate_cvt, in_samples, &max_out);
        ESP_RETURN_ON_FALSE(ae_ret == ESP_AE_ERR_OK, ESP_FAIL, TAG, "Failed to query rate convert size");
        size_t new_size = (size_t)max_out * ctx->dst_ch * audio_bytes_per_sample(ctx->dst_bits);
        if (new_size > ctx->rate_buf_size) {
            uint8_t *buf = realloc(ctx->rate_buf, new_size);
            ESP_RETURN_ON_FALSE(buf != NULL, ESP_ERR_NO_MEM, TAG, "No memory for rate convert buffer");
            ctx->rate_buf = buf;
            ctx->rate_buf_size = new_size;
        }
        ctx->max_out_samples = max_out;
    }
    ctx->input_sample_capacity = in_samples;
    return ESP_OK;
}

static void audio_convert_deinit(audio_convert_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->rate_cvt) {
        esp_ae_rate_cvt_close(ctx->rate_cvt);
        ctx->rate_cvt = NULL;
    }
    if (ctx->bit_cvt) {
        esp_ae_bit_cvt_close(ctx->bit_cvt);
        ctx->bit_cvt = NULL;
    }
    if (ctx->ch_cvt) {
        esp_ae_ch_cvt_close(ctx->ch_cvt);
        ctx->ch_cvt = NULL;
    }
    SAFE_FREE(ctx->ch_buf);
    SAFE_FREE(ctx->bit_buf);
    SAFE_FREE(ctx->rate_buf);
    memset(ctx, 0, sizeof(*ctx));
}

static esp_err_t audio_convert_init(audio_convert_ctx_t *ctx,
                                    uint32_t src_rate, uint8_t src_bits, uint8_t src_ch,
                                    uint32_t dst_rate, uint8_t dst_bits, uint8_t dst_ch,
                                    uint32_t max_in_samples)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid convert context");
    memset(ctx, 0, sizeof(*ctx));
    ctx->src_rate = src_rate;
    ctx->dst_rate = dst_rate;
    ctx->src_bits = src_bits;
    ctx->dst_bits = dst_bits;
    ctx->src_ch = src_ch;
    ctx->dst_ch = dst_ch;
    ctx->input_sample_capacity = max_in_samples;
    ctx->bypass = (src_rate == dst_rate && src_bits == dst_bits && src_ch == dst_ch);
    if (ctx->bypass) {
        return ESP_OK;
    }

    if (src_ch != dst_ch) {
        esp_ae_ch_cvt_cfg_t cfg = {
            .sample_rate = src_rate,
            .bits_per_sample = src_bits,
            .src_ch = src_ch,
            .dest_ch = dst_ch,
            .weight = NULL,
            .weight_len = 0,
        };
        ESP_RETURN_ON_FALSE(esp_ae_ch_cvt_open(&cfg, &ctx->ch_cvt) == ESP_AE_ERR_OK,
                            ESP_FAIL, TAG, "Failed to open channel convert");
        ctx->ch_buf_size = (size_t)max_in_samples * dst_ch * audio_bytes_per_sample(src_bits);
        ctx->ch_buf = calloc(1, ctx->ch_buf_size);
        ESP_GOTO_ON_FALSE(ctx->ch_buf != NULL, ESP_ERR_NO_MEM, err, TAG, "No memory for channel buffer");
    }
    if (src_bits != dst_bits) {
        esp_ae_bit_cvt_cfg_t cfg = {
            .sample_rate = src_rate,
            .channel = dst_ch,
            .src_bits = src_bits,
            .dest_bits = dst_bits,
        };
        ESP_RETURN_ON_FALSE(esp_ae_bit_cvt_open(&cfg, &ctx->bit_cvt) == ESP_AE_ERR_OK,
                            ESP_FAIL, TAG, "Failed to open bit convert");
        ctx->bit_buf_size = (size_t)max_in_samples * dst_ch * audio_bytes_per_sample(dst_bits);
        ctx->bit_buf = calloc(1, ctx->bit_buf_size);
        ESP_GOTO_ON_FALSE(ctx->bit_buf != NULL, ESP_ERR_NO_MEM, err, TAG, "No memory for bit buffer");
    }
    if (src_rate != dst_rate) {
        esp_ae_rate_cvt_cfg_t cfg = {
            .src_rate = src_rate,
            .dest_rate = dst_rate,
            .channel = dst_ch,
            .bits_per_sample = dst_bits,
            .complexity = 2,
            .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY,
        };
        ESP_RETURN_ON_FALSE(esp_ae_rate_cvt_open(&cfg, &ctx->rate_cvt) == ESP_AE_ERR_OK,
                            ESP_FAIL, TAG, "Failed to open rate convert");
        ESP_RETURN_ON_FALSE(esp_ae_rate_cvt_get_max_out_sample_num(ctx->rate_cvt, max_in_samples, &ctx->max_out_samples) == ESP_AE_ERR_OK,
                            ESP_FAIL, TAG, "Failed to query max rate convert output");
        ctx->rate_buf_size = (size_t)ctx->max_out_samples * dst_ch * audio_bytes_per_sample(dst_bits);
        ctx->rate_buf = calloc(1, ctx->rate_buf_size);
        ESP_GOTO_ON_FALSE(ctx->rate_buf != NULL, ESP_ERR_NO_MEM, err, TAG, "No memory for rate buffer");
    }
    return ESP_OK;

err:
    audio_convert_deinit(ctx);
    return ret;
}

static esp_err_t audio_convert_process(audio_convert_ctx_t *ctx, const uint8_t *in_buf, size_t in_size,
                                       uint8_t **out_buf, size_t *out_size)
{
    ESP_RETURN_ON_FALSE(ctx != NULL && in_buf != NULL && out_buf != NULL && out_size != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid convert args");
    *out_buf = NULL;
    *out_size = 0;
    if (in_size == 0) {
        return ESP_OK;
    }
    if (ctx->bypass) {
        *out_buf = (uint8_t *)in_buf;
        *out_size = in_size;
        return ESP_OK;
    }

    uint32_t in_samples = in_size / audio_bytes_per_frame(ctx->src_bits, ctx->src_ch);
    ESP_RETURN_ON_FALSE(in_samples > 0, ESP_ERR_INVALID_SIZE, TAG, "Invalid convert input size");
    ESP_RETURN_ON_ERROR(audio_convert_reserve(ctx, in_samples), TAG, "Failed to reserve convert buffers");

    const uint8_t *cur_buf = in_buf;
    uint32_t cur_samples = in_samples;
    uint8_t *stage_buf = NULL;

    if (ctx->ch_cvt) {
        ESP_RETURN_ON_FALSE(esp_ae_ch_cvt_process(ctx->ch_cvt, cur_samples, (esp_ae_sample_t)cur_buf,
                                                  (esp_ae_sample_t)ctx->ch_buf) == ESP_AE_ERR_OK,
                            ESP_FAIL, TAG, "Channel convert failed");
        cur_buf = ctx->ch_buf;
    }
    if (ctx->bit_cvt) {
        ESP_RETURN_ON_FALSE(esp_ae_bit_cvt_process(ctx->bit_cvt, cur_samples, (esp_ae_sample_t)cur_buf,
                                                   (esp_ae_sample_t)ctx->bit_buf) == ESP_AE_ERR_OK,
                            ESP_FAIL, TAG, "Bit convert failed");
        cur_buf = ctx->bit_buf;
    }
    if (ctx->rate_cvt) {
        uint32_t out_samples = ctx->max_out_samples;
        ESP_RETURN_ON_FALSE(esp_ae_rate_cvt_process(ctx->rate_cvt, (esp_ae_sample_t)cur_buf, cur_samples,
                                                    (esp_ae_sample_t)ctx->rate_buf, &out_samples) == ESP_AE_ERR_OK,
                            ESP_FAIL, TAG, "Rate convert failed");
        cur_samples = out_samples;
        stage_buf = ctx->rate_buf;
    } else if (ctx->bit_cvt) {
        stage_buf = ctx->bit_buf;
    } else if (ctx->ch_cvt) {
        stage_buf = ctx->ch_buf;
    }

    *out_buf = stage_buf;
    *out_size = (size_t)cur_samples * ctx->dst_ch * audio_bytes_per_sample(ctx->dst_bits);
    return ESP_OK;
}

static esp_audio_type_t audio_format_to_codec_type(av_processor_format_id_t format)
{
    switch (format) {
        case AV_PROCESSOR_FORMAT_ID_PCM:
            return ESP_AUDIO_TYPE_PCM;
        case AV_PROCESSOR_FORMAT_ID_G711A:
            return ESP_AUDIO_TYPE_G711A;
        case AV_PROCESSOR_FORMAT_ID_G711U:
            return ESP_AUDIO_TYPE_G711U;
        case AV_PROCESSOR_FORMAT_ID_OPUS:
            return ESP_AUDIO_TYPE_OPUS;
        case AV_PROCESSOR_FORMAT_ID_AAC:
            return ESP_AUDIO_TYPE_AAC;
        case AV_PROCESSOR_FORMAT_ID_MP3:
            return ESP_AUDIO_TYPE_MP3;
        default:
            return ESP_AUDIO_TYPE_UNSUPPORT;
    }
}

static const av_processor_audio_info_t *audio_encoder_info_from_config(const av_processor_encoder_config_t *cfg)
{
    if (cfg == NULL) {
        return NULL;
    }
    switch (cfg->format) {
        case AV_PROCESSOR_FORMAT_ID_G711A:
        case AV_PROCESSOR_FORMAT_ID_G711U:
            return &cfg->params.g711.audio_info;
        case AV_PROCESSOR_FORMAT_ID_OPUS:
            return &cfg->params.opus.audio_info;
        case AV_PROCESSOR_FORMAT_ID_PCM:
            return &cfg->params.pcm.audio_info;
        case AV_PROCESSOR_FORMAT_ID_AAC:
            return &cfg->params.aac.audio_info;
        default:
            return NULL;
    }
}

static const av_processor_audio_info_t *audio_decoder_info_from_config(const av_processor_decoder_config_t *cfg)
{
    if (cfg == NULL) {
        return NULL;
    }
    switch (cfg->format) {
        case AV_PROCESSOR_FORMAT_ID_G711A:
        case AV_PROCESSOR_FORMAT_ID_G711U:
            return &cfg->params.g711.audio_info;
        case AV_PROCESSOR_FORMAT_ID_OPUS:
            return &cfg->params.opus.audio_info;
        case AV_PROCESSOR_FORMAT_ID_PCM:
            return &cfg->params.pcm.audio_info;
        case AV_PROCESSOR_FORMAT_ID_AAC:
            return &cfg->params.aac.audio_info;
        default:
            return NULL;
    }
}

static esp_opus_enc_frame_duration_t audio_map_opus_enc_frame_duration(uint8_t duration_ms)
{
    switch (duration_ms) {
        case 3:
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
            return ESP_OPUS_ENC_FRAME_DURATION_20_MS;
    }
}

static esp_opus_dec_frame_duration_t audio_map_opus_dec_frame_duration(uint8_t duration_ms)
{
    switch (duration_ms) {
        case 3:
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

static esp_err_t audio_recorder_push_packet(audio_recorder_handle_t rec, const uint8_t *data, size_t len)
{
    static uint32_t s_recorder_packet_log_count = 0;

    if (s_recorder_packet_log_count < 40) {
        s_recorder_packet_log_count++;
        ESP_LOGI(TAG, "Recorder packet #%" PRIu32 " len=%u", s_recorder_packet_log_count, (unsigned)len);
    }

    audio_light_packet_t *pkt = audio_light_packet_alloc(data, len);
    ESP_RETURN_ON_FALSE(pkt != NULL, ESP_ERR_NO_MEM, TAG, "Failed to alloc recorder packet");
    if (xQueueSend(rec->out_queue, &pkt, 0) == pdTRUE) {
        return ESP_OK;
    }
    audio_light_packet_t *dropped_pkt = NULL;
    if (xQueueReceive(rec->out_queue, &dropped_pkt, 0) == pdTRUE) {
        audio_light_packet_free(dropped_pkt);
    }
    if (xQueueSend(rec->out_queue, &pkt, 0) == pdTRUE) {
        return ESP_OK;
    }
    audio_light_packet_free(pkt);
    return ESP_FAIL;
}

static esp_err_t audio_recorder_push_stream(audio_recorder_handle_t rec, const uint8_t *data, size_t len)
{
    size_t offset = 0;

    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > AUDIO_LIGHT_PACKET_MAX) {
            chunk = AUDIO_LIGHT_PACKET_MAX;
        }
        ESP_RETURN_ON_ERROR(audio_recorder_push_packet(rec, data + offset, chunk), TAG, "Failed to push recorder stream");
        offset += chunk;
    }
    return ESP_OK;
}

static esp_err_t audio_recorder_init_wakenet(audio_recorder_handle_t rec)
{
    const char *partition = rec->config.afe_config.model ? rec->config.afe_config.model : "model";
    rec->models = esp_srmodel_init(partition);
    if (rec->models == NULL && strcmp(partition, "model") != 0) {
        rec->models = esp_srmodel_init("model");
    }
    ESP_RETURN_ON_FALSE(rec->models != NULL, ESP_FAIL, TAG, "Failed to init SR models");

    char *model_name = esp_srmodel_filter(rec->models, ESP_WN_PREFIX, NULL);
    ESP_RETURN_ON_FALSE(model_name != NULL, ESP_FAIL, TAG, "No WakeNet model found");
    rec->wn_iface = esp_wn_handle_from_name(model_name);
    ESP_RETURN_ON_FALSE(rec->wn_iface != NULL, ESP_FAIL, TAG, "Failed to get WakeNet iface");
    rec->wn_model = rec->wn_iface->create(model_name, DET_MODE_90);
    ESP_RETURN_ON_FALSE(rec->wn_model != NULL, ESP_FAIL, TAG, "Failed to create WakeNet model");

    ESP_RETURN_ON_FALSE(rec->wn_iface->get_channel_num(rec->wn_model) == 1,
                        ESP_ERR_NOT_SUPPORTED, TAG, "Light recorder supports mono WakeNet only");
    ESP_RETURN_ON_FALSE(rec->wn_iface->get_samp_rate(rec->wn_model) == AUDIO_LIGHT_WN_RATE,
                        ESP_ERR_NOT_SUPPORTED, TAG, "Light recorder supports 16k WakeNet only");

    rec->wn_frame_samples = rec->wn_iface->get_samp_chunksize(rec->wn_model);
    rec->wn_frame_bytes = (size_t)rec->wn_frame_samples * audio_bytes_per_sample(AUDIO_LIGHT_WN_BITS);
    rec->wn_cache_cap = rec->wn_frame_bytes * 4;
    rec->wn_cache = calloc(1, rec->wn_cache_cap);
    ESP_RETURN_ON_FALSE(rec->wn_cache != NULL, ESP_ERR_NO_MEM, TAG, "No memory for WakeNet cache");
    return ESP_OK;
}

static esp_err_t audio_recorder_init_encoder(audio_recorder_handle_t rec)
{
    const av_processor_audio_info_t *enc_info = audio_encoder_info_from_config(&rec->config.encoder_cfg);

    ESP_RETURN_ON_FALSE(enc_info != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid recorder encoder config");
    int frame_duration_ms = enc_info->frame_duration ? enc_info->frame_duration : 20;
    ESP_RETURN_ON_FALSE(enc_info->sample_rate > 0 && enc_info->channels > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid encoder info");
    ESP_RETURN_ON_FALSE(enc_info->sample_bits == 16, ESP_ERR_NOT_SUPPORTED, TAG, "Light recorder supports 16-bit encoder input only");
    ESP_RETURN_ON_FALSE(rec->config.encoder_cfg.format == AV_PROCESSOR_FORMAT_ID_PCM ||
                        rec->config.encoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711A ||
                        rec->config.encoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711U ||
                        rec->config.encoder_cfg.format == AV_PROCESSOR_FORMAT_ID_OPUS,
                        ESP_ERR_NOT_SUPPORTED, TAG, "Light recorder supports PCM/G711/OPUS only");

    if (rec->config.encoder_cfg.format == AV_PROCESSOR_FORMAT_ID_PCM) {
        rec->enc_in_bytes = enc_info->sample_rate * enc_info->channels *
                            audio_bytes_per_sample(enc_info->sample_bits) * frame_duration_ms / 1000;
        if (rec->enc_in_bytes <= 0) {
            rec->enc_in_bytes = rec->wn_frame_bytes;
        }
        rec->enc_out_bytes = rec->enc_in_bytes;
        rec->enc_pcm_cap = rec->enc_in_bytes * 4;
        rec->enc_pcm_cache = calloc(1, rec->enc_pcm_cap);
        ESP_RETURN_ON_FALSE(rec->enc_pcm_cache != NULL, ESP_ERR_NO_MEM, TAG, "No memory for PCM cache");
        return ESP_OK;
    }

    esp_audio_enc_config_t enc_cfg = {
        .type = audio_format_to_codec_type(rec->config.encoder_cfg.format),
        .cfg = NULL,
        .cfg_sz = 0,
    };
    esp_g711_enc_config_t g711_cfg = ESP_G711_ENC_CONFIG_DEFAULT();
    esp_opus_enc_config_t opus_cfg = ESP_OPUS_ENC_CONFIG_DEFAULT();

    if (rec->config.encoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711A ||
        rec->config.encoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711U) {
        g711_cfg.sample_rate = enc_info->sample_rate;
        g711_cfg.channel = enc_info->channels;
        g711_cfg.bits_per_sample = enc_info->sample_bits;
        g711_cfg.frame_duration = frame_duration_ms;
        enc_cfg.cfg = &g711_cfg;
        enc_cfg.cfg_sz = sizeof(g711_cfg);
    } else {
        opus_cfg.sample_rate = enc_info->sample_rate;
        opus_cfg.channel = enc_info->channels;
        opus_cfg.bits_per_sample = enc_info->sample_bits;
        opus_cfg.bitrate = rec->config.encoder_cfg.params.opus.bitrate;
        opus_cfg.enable_vbr = rec->config.encoder_cfg.params.opus.enable_vbr;
        opus_cfg.frame_duration = audio_map_opus_enc_frame_duration(frame_duration_ms);
        enc_cfg.cfg = &opus_cfg;
        enc_cfg.cfg_sz = sizeof(opus_cfg);
    }

    ESP_RETURN_ON_FALSE(esp_audio_enc_open(&enc_cfg, &rec->encoder) == ESP_AUDIO_ERR_OK,
                        ESP_FAIL, TAG, "Failed to open encoder");

    int in_size = 0;
    int out_size = 0;
    ESP_RETURN_ON_FALSE(esp_audio_enc_get_frame_size(rec->encoder, &in_size, &out_size) == ESP_AUDIO_ERR_OK,
                        ESP_FAIL, TAG, "Failed to query encoder frame size");

    if (rec->config.encoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711A ||
        rec->config.encoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711U) {
        rec->enc_in_bytes = enc_info->sample_rate * enc_info->channels * audio_bytes_per_sample(enc_info->sample_bits) * frame_duration_ms / 1000;
        int unit_count = rec->enc_in_bytes / (in_size > 0 ? in_size : 1);
        rec->enc_out_bytes = unit_count * (out_size > 0 ? out_size : 1);
    } else {
        rec->enc_in_bytes = in_size;
        rec->enc_out_bytes = out_size;
    }
    ESP_RETURN_ON_FALSE(rec->enc_in_bytes > 0 && rec->enc_out_bytes > 0,
                        ESP_FAIL, TAG, "Invalid encoder frame size");

    rec->enc_out_buf_size = rec->enc_out_bytes + 64;
    rec->enc_out_buf = calloc(1, rec->enc_out_buf_size);
    ESP_RETURN_ON_FALSE(rec->enc_out_buf != NULL, ESP_ERR_NO_MEM, TAG, "No memory for encoder output");

    rec->enc_pcm_cap = rec->enc_in_bytes * 4;
    rec->enc_pcm_cache = calloc(1, rec->enc_pcm_cap);
    ESP_RETURN_ON_FALSE(rec->enc_pcm_cache != NULL, ESP_ERR_NO_MEM, TAG, "No memory for encoder PCM cache");
    return ESP_OK;
}

static esp_err_t audio_recorder_encode_pcm(audio_recorder_handle_t rec, const uint8_t *pcm_16k_mono, size_t pcm_size)
{
    uint8_t *enc_pcm = NULL;
    size_t enc_pcm_size = 0;
    ESP_RETURN_ON_ERROR(audio_convert_process(&rec->to_enc_cvt, pcm_16k_mono, pcm_size, &enc_pcm, &enc_pcm_size),
                        TAG, "Recorder encoder convert failed");
    if (enc_pcm_size == 0) {
        return ESP_OK;
    }
    if (rec->enc_pcm_len + enc_pcm_size > rec->enc_pcm_cap) {
        size_t new_cap = rec->enc_pcm_len + enc_pcm_size + rec->enc_in_bytes;
        uint8_t *new_buf = realloc(rec->enc_pcm_cache, new_cap);
        ESP_RETURN_ON_FALSE(new_buf != NULL, ESP_ERR_NO_MEM, TAG, "No memory for encoder cache grow");
        rec->enc_pcm_cache = new_buf;
        rec->enc_pcm_cap = new_cap;
    }
    memcpy(rec->enc_pcm_cache + rec->enc_pcm_len, enc_pcm, enc_pcm_size);
    rec->enc_pcm_len += enc_pcm_size;

    if (rec->config.encoder_cfg.format == AV_PROCESSOR_FORMAT_ID_PCM) {
        while (rec->enc_pcm_len >= (size_t)rec->enc_in_bytes) {
            ESP_RETURN_ON_ERROR(audio_recorder_push_stream(rec, rec->enc_pcm_cache, rec->enc_in_bytes),
                                TAG, "Failed to queue recorder PCM");
            rec->enc_pcm_len -= rec->enc_in_bytes;
            if (rec->enc_pcm_len > 0) {
                memmove(rec->enc_pcm_cache, rec->enc_pcm_cache + rec->enc_in_bytes, rec->enc_pcm_len);
            }
        }
        return ESP_OK;
    }

    while (rec->enc_pcm_len >= (size_t)rec->enc_in_bytes) {
        esp_audio_enc_in_frame_t in_frame = {
            .buffer = rec->enc_pcm_cache,
            .len = rec->enc_in_bytes,
        };
        esp_audio_enc_out_frame_t out_frame = {
            .buffer = rec->enc_out_buf,
            .len = rec->enc_out_buf_size,
            .encoded_bytes = 0,
        };
        ESP_RETURN_ON_FALSE(esp_audio_enc_process(rec->encoder, &in_frame, &out_frame) == ESP_AUDIO_ERR_OK,
                            ESP_FAIL, TAG, "Recorder encode failed");
        if (out_frame.encoded_bytes > 0) {
            ESP_RETURN_ON_ERROR(audio_recorder_push_packet(rec, rec->enc_out_buf, out_frame.encoded_bytes),
                                TAG, "Failed to queue recorder packet");
        }
        rec->enc_pcm_len -= rec->enc_in_bytes;
        if (rec->enc_pcm_len > 0) {
            memmove(rec->enc_pcm_cache, rec->enc_pcm_cache + rec->enc_in_bytes, rec->enc_pcm_len);
        }
    }
    return ESP_OK;
}

static esp_err_t audio_recorder_process_input(audio_recorder_handle_t rec)
{
    ESP_RETURN_ON_FALSE(rec != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid recorder");
    ESP_RETURN_ON_FALSE(audio_manager_read_input(rec->input_buf, rec->input_buf_size) == ESP_OK,
                        ESP_FAIL, TAG, "Recorder input read failed");

    uint8_t *wn_pcm = NULL;
    size_t wn_pcm_size = 0;
    ESP_RETURN_ON_ERROR(audio_convert_process(&rec->to_wn_cvt, rec->input_buf, rec->input_buf_size, &wn_pcm, &wn_pcm_size),
                        TAG, "Recorder WakeNet convert failed");
    if (wn_pcm_size == 0) {
        return ESP_OK;
    }

    if (rec->wn_cache_len + wn_pcm_size > rec->wn_cache_cap) {
        size_t new_cap = rec->wn_cache_len + wn_pcm_size + rec->wn_frame_bytes;
        uint8_t *new_buf = realloc(rec->wn_cache, new_cap);
        ESP_RETURN_ON_FALSE(new_buf != NULL, ESP_ERR_NO_MEM, TAG, "No memory for WakeNet cache grow");
        rec->wn_cache = new_buf;
        rec->wn_cache_cap = new_cap;
    }
    memcpy(rec->wn_cache + rec->wn_cache_len, wn_pcm, wn_pcm_size);
    rec->wn_cache_len += wn_pcm_size;

    while (rec->wn_cache_len >= rec->wn_frame_bytes) {
        wakenet_state_t state = rec->wn_iface->detect(rec->wn_model, (int16_t *)rec->wn_cache);
        if (state == WAKENET_DETECTED) {
            rec->wn_event.trigger_ch = rec->wn_iface->get_triggered_channel(rec->wn_model);
            ESP_LOGI(TAG, "Wake word detected on channel %" PRIi32, rec->wn_event.trigger_ch);
            if (rec->config.recorder_event_cb) {
                rec->config.recorder_event_cb(rec, &rec->wn_event, rec->config.recorder_ctx);
            }
        }
        rec->wn_cache_len -= rec->wn_frame_bytes;
        if (rec->wn_cache_len > 0) {
            memmove(rec->wn_cache, rec->wn_cache + rec->wn_frame_bytes, rec->wn_cache_len);
        }
    }

    return audio_recorder_encode_pcm(rec, wn_pcm, wn_pcm_size);
}

static esp_err_t audio_feeder_write_pcm(audio_feeder_handle_t fd, const uint8_t *pcm, size_t pcm_size)
{
    uint8_t *board_pcm = NULL;
    size_t board_pcm_size = 0;
    ESP_RETURN_ON_ERROR(audio_convert_process(&fd->to_board_cvt, pcm, pcm_size, &board_pcm, &board_pcm_size),
                        TAG, "Feeder output convert failed");
    if (board_pcm_size == 0 || audio_manager_any_prompt_playing()) {
        return ESP_OK;
    }
    return audio_manager_write_output(board_pcm, board_pcm_size);
}

static esp_err_t audio_feeder_process_packet(audio_feeder_handle_t fd, const audio_light_packet_t *pkt)
{
    if (fd->config.decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_PCM) {
        return audio_feeder_write_pcm(fd, pkt->data, pkt->len);
    }

    esp_audio_dec_in_raw_t raw = {
        .buffer = (uint8_t *)pkt->data,
        .len = pkt->len,
        .consumed = 0,
    };
    while (raw.len > 0) {
        esp_audio_dec_out_frame_t frame = {
            .buffer = fd->dec_out_buf,
            .len = fd->dec_out_buf_size,
            .needed_size = 0,
            .decoded_size = 0,
        };
        esp_audio_err_t ret = esp_audio_dec_process(fd->decoder, &raw, &frame);
        if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            uint8_t *new_buf = realloc(fd->dec_out_buf, frame.needed_size);
            ESP_RETURN_ON_FALSE(new_buf != NULL, ESP_ERR_NO_MEM, TAG, "No memory for feeder decode output");
            fd->dec_out_buf = new_buf;
            fd->dec_out_buf_size = frame.needed_size;
            continue;
        }
        ESP_RETURN_ON_FALSE(ret == ESP_AUDIO_ERR_OK, ESP_FAIL, TAG, "Decoder process failed");
        if (frame.decoded_size > 0) {
            ESP_RETURN_ON_ERROR(audio_feeder_write_pcm(fd, fd->dec_out_buf, frame.decoded_size),
                                TAG, "Failed to write feeder PCM");
        }
        if (raw.consumed == 0) {
            break;
        }
        raw.buffer += raw.consumed;
        raw.len -= raw.consumed;
        raw.consumed = 0;
    }
    return ESP_OK;
}

static void audio_worker_task(void *arg)
{
    (void)arg;
    while (true) {
        audio_recorder_handle_t rec = NULL;
        audio_feeder_handle_t fd = NULL;
        bool stop = false;
        if (audio_manager.lock) {
            xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
            rec = audio_manager.recorder;
            fd = audio_manager.feeder;
            audio_manager.recorder_busy = rec != NULL;
            audio_manager.feeder_busy = fd != NULL;
            stop = audio_manager.worker_stop;
            xSemaphoreGive(audio_manager.lock);
        }

        if (stop && rec == NULL && fd == NULL) {
            break;
        }

        bool did_work = false;
        if (fd && fd->state == AUDIO_RUN_STATE_PLAYING) {
            audio_light_packet_t *pkt = NULL;
            if (xQueueReceive(fd->in_queue, &pkt, 0) == pdTRUE) {
                if (audio_feeder_process_packet(fd, pkt) != ESP_OK) {
                    ESP_LOGE(TAG, "Feeder packet processing failed");
                }
                audio_light_packet_free(pkt);
                did_work = true;
            }
        }

        if (rec && rec->state == AUDIO_RUN_STATE_PLAYING) {
            if (audio_recorder_process_input(rec) != ESP_OK) {
                ESP_LOGW(TAG, "Recorder processing failed");
                vTaskDelay(pdMS_TO_TICKS(AUDIO_LIGHT_POLL_MS));
            } else {
                did_work = true;
            }
        }

        if (!did_work) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AUDIO_LIGHT_POLL_MS));
        }

        if (audio_manager.lock) {
            xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
            audio_manager.recorder_busy = false;
            audio_manager.feeder_busy = false;
            xSemaphoreGive(audio_manager.lock);
        }
    }

    if (audio_manager.lock) {
        xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
        audio_manager.worker_task = NULL;
        audio_manager.worker_stop = false;
        audio_manager.recorder_busy = false;
        audio_manager.feeder_busy = false;
        xSemaphoreGive(audio_manager.lock);
    }
    vTaskDelete(NULL);
}

static esp_err_t audio_worker_ensure_started(const audio_task_config_t *task_cfg)
{
    if (audio_manager.lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
    if (audio_manager.worker_task) {
        audio_manager.worker_stop = false;
        xSemaphoreGive(audio_manager.lock);
        return ESP_OK;
    }

    int stack = AUDIO_LIGHT_WORKER_STACK_DEFAULT;
    int prio = AUDIO_LIGHT_WORKER_PRIO_DEFAULT;
    BaseType_t core = tskNO_AFFINITY;
    if (task_cfg) {
        if (task_cfg->task_stack > stack) {
            stack = task_cfg->task_stack;
        }
        if (task_cfg->task_prio > 0) {
            prio = task_cfg->task_prio;
        }
        if (task_cfg->task_core >= 0) {
            core = task_cfg->task_core;
        }
    }
    BaseType_t ok = xTaskCreatePinnedToCore(audio_worker_task, "audio_light_worker",
                                            stack, NULL, prio, &audio_manager.worker_task, core);
    if (ok != pdPASS) {
        audio_manager.worker_task = NULL;
        xSemaphoreGive(audio_manager.lock);
        ESP_LOGE(TAG, "Failed to create light worker task");
        return ESP_ERR_NO_MEM;
    }
    audio_manager.worker_stop = false;
    xSemaphoreGive(audio_manager.lock);
    return ESP_OK;
}

static void audio_worker_notify(void)
{
    if (audio_manager.worker_task) {
        xTaskNotifyGive(audio_manager.worker_task);
    }
}

static void audio_worker_request_stop_if_idle(void)
{
    if (audio_manager.lock == NULL) {
        return;
    }
    xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
    if (audio_manager.recorder == NULL && audio_manager.feeder == NULL && audio_manager.worker_task) {
        audio_manager.worker_stop = true;
        xTaskNotifyGive(audio_manager.worker_task);
    }
    xSemaphoreGive(audio_manager.lock);
}

static esp_err_t audio_play_internal(audio_play_handle_t player, const char *url, bool block_until_done)
{
    if (!player || !url) {
        ESP_LOGE(TAG, "Invalid parameters for play");
        return ESP_FAIL;
    }
    if (player->state == AUDIO_RUN_STATE_CLOSED) {
        ESP_LOGE(TAG, "Player not opened");
        return ESP_FAIL;
    }
    if (player->state == AUDIO_RUN_STATE_PLAYING) {
        esp_audio_simple_player_stop(player->player);
    }
    player->state = AUDIO_RUN_STATE_PLAYING;

    esp_err_t err = block_until_done ? esp_audio_simple_player_run_to_end(player->player, url, NULL) :
                                       esp_audio_simple_player_run(player->player, url, NULL);
    if (err != ESP_OK) {
        player->state = AUDIO_RUN_STATE_IDLE;
        ESP_LOGE(TAG, "Failed to start player");
        return ESP_FAIL;
    }
    if (block_until_done) {
        player->state = AUDIO_RUN_STATE_IDLE;
    }
    return ESP_OK;
}

esp_err_t audio_manager_init(audio_manager_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid audio manager config");
    audio_manager.config = *config;
    if (audio_manager.lock == NULL) {
        audio_manager.lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(audio_manager.lock != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create audio manager lock");
    }
    audio_manager.player = NULL;
    audio_manager.recorder = NULL;
    audio_manager.feeder = NULL;
    audio_manager.recorder_busy = false;
    audio_manager.feeder_busy = false;
    audio_manager.worker_stop = false;

    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();
    esp_audio_enc_register_default();
    ESP_LOGI(TAG, "Audio manager initialized (light implementation)");
    return ESP_OK;
}

esp_err_t audio_manager_deinit(void)
{
    audio_manager.player = NULL;
    audio_manager.recorder = NULL;
    audio_manager.feeder = NULL;
    esp_audio_dec_unregister_default();
    esp_audio_simple_dec_unregister_default();
    esp_audio_enc_unregister_default();
    ESP_LOGI(TAG, "Audio manager deinitialized");
    return ESP_OK;
}

esp_err_t audio_playback_open(audio_play_config_t *config, audio_play_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(config != NULL && handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid playback config");
    *handle = NULL;
    ESP_RETURN_ON_FALSE(audio_manager.player == NULL, ESP_ERR_INVALID_STATE, TAG, "Only one light player is supported");

    audio_play_handle_t player = calloc(1, sizeof(*player));
    ESP_RETURN_ON_FALSE(player != NULL, ESP_ERR_NO_MEM, TAG, "No memory for player");
    player->state = AUDIO_RUN_STATE_CLOSED;
    player->config = *config;

    if (av_audio_playback_pool_init(&player->pool) != ESP_OK) {
        SAFE_FREE(player);
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
    if (esp_audio_simple_player_new(&cfg, &player->player) != ESP_OK || player->player == NULL) {
        (void)audio_player_release_resources(&player->player, &player->pool, "audio");
        SAFE_FREE(player);
        return ESP_FAIL;
    }
    if (esp_audio_simple_player_set_event(player->player, audio_play_event_callback, player) != ESP_OK) {
        (void)audio_player_release_resources(&player->player, &player->pool, "audio");
        SAFE_FREE(player);
        return ESP_FAIL;
    }
    player->state = AUDIO_RUN_STATE_IDLE;
    if (audio_manager.lock) {
        xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
        audio_manager.player = player;
        xSemaphoreGive(audio_manager.lock);
    } else {
        audio_manager.player = player;
    }
    *handle = player;
    ESP_LOGI(TAG, "Audio %s opened", audio_play_type_to_str(config->type));
    return ESP_OK;
}

esp_err_t audio_playback_close(audio_play_handle_t player)
{
    if (!player) {
        return ESP_OK;
    }
    if (player->state == AUDIO_RUN_STATE_CLOSED) {
        SAFE_FREE(player);
        return ESP_OK;
    }
    if (player->state == AUDIO_RUN_STATE_PLAYING) {
        esp_audio_simple_player_stop(player->player);
    }
    if (audio_player_release_resources(&player->player, &player->pool, "audio") != ESP_OK) {
        return ESP_FAIL;
    }
    if (audio_manager.lock) {
        xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
        if (audio_manager.player == player) {
            audio_manager.player = NULL;
        }
        xSemaphoreGive(audio_manager.lock);
    } else if (audio_manager.player == player) {
        audio_manager.player = NULL;
    }
    SAFE_FREE(player);
    return ESP_OK;
}

esp_err_t audio_playback_play(audio_play_handle_t player, const char *url)
{
    return audio_play_internal(player, url, player && player->config.block_until_done);
}

esp_err_t audio_playback_pause(audio_play_handle_t player)
{
    ESP_RETURN_ON_FALSE(player != NULL && player->state != AUDIO_RUN_STATE_CLOSED, ESP_FAIL, TAG, "Player not opened");
    ESP_RETURN_ON_FALSE(!audio_play_is_prompt(player), ESP_ERR_NOT_SUPPORTED, TAG, "Prompt player does not support pause");
    if (player->state == AUDIO_RUN_STATE_PAUSED) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(player->state == AUDIO_RUN_STATE_PLAYING, ESP_FAIL, TAG, "Player not playing");
    ESP_RETURN_ON_FALSE(esp_audio_simple_player_pause(player->player) == ESP_OK, ESP_FAIL, TAG, "Pause failed");
    player->state = AUDIO_RUN_STATE_PAUSED;
    return ESP_OK;
}

esp_err_t audio_playback_resume(audio_play_handle_t player)
{
    ESP_RETURN_ON_FALSE(player != NULL && player->state != AUDIO_RUN_STATE_CLOSED, ESP_FAIL, TAG, "Player not opened");
    ESP_RETURN_ON_FALSE(!audio_play_is_prompt(player), ESP_ERR_NOT_SUPPORTED, TAG, "Prompt player does not support resume");
    if (player->state == AUDIO_RUN_STATE_PLAYING) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(player->state == AUDIO_RUN_STATE_PAUSED, ESP_FAIL, TAG, "Player not paused");
    ESP_RETURN_ON_FALSE(esp_audio_simple_player_resume(player->player) == ESP_OK, ESP_FAIL, TAG, "Resume failed");
    player->state = AUDIO_RUN_STATE_PLAYING;
    return ESP_OK;
}

esp_err_t audio_playback_stop(audio_play_handle_t player)
{
    ESP_RETURN_ON_FALSE(player != NULL && player->state != AUDIO_RUN_STATE_CLOSED, ESP_FAIL, TAG, "Player not opened");
    if (player->state == AUDIO_RUN_STATE_IDLE) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(esp_audio_simple_player_stop(player->player) == ESP_OK, ESP_FAIL, TAG, "Stop failed");
    player->state = AUDIO_RUN_STATE_IDLE;
    return ESP_OK;
}

esp_err_t audio_recorder_open(audio_recorder_config_t *config, audio_recorder_handle_t *handle)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(config != NULL && handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid recorder config");
    *handle = NULL;
    ESP_RETURN_ON_FALSE(config->afe_config.frontend_type == AV_PROCESSOR_FRONTEND_TYPE_CUSTOM,
                        ESP_ERR_NOT_SUPPORTED, TAG, "Light recorder supports custom frontend only");
    ESP_RETURN_ON_FALSE((config->afe_config.mode.custom.algo_mask & AV_PROCESSOR_CUSTOM_FLAG_WAKENET_ENABLE) != 0,
                        ESP_ERR_NOT_SUPPORTED, TAG, "Light recorder requires WakeNet enable");
    ESP_RETURN_ON_FALSE(audio_manager.recorder == NULL, ESP_ERR_INVALID_STATE, TAG, "Only one light recorder is supported");

    audio_recorder_handle_t rec = calloc(1, sizeof(*rec));
    ESP_RETURN_ON_FALSE(rec != NULL, ESP_ERR_NO_MEM, TAG, "No memory for recorder");
    rec->state = AUDIO_RUN_STATE_CLOSED;
    rec->config = *config;
    rec->out_queue = xQueueCreate(AUDIO_LIGHT_REC_QUEUE_LEN, sizeof(audio_light_packet_t *));
    ESP_GOTO_ON_FALSE(rec->out_queue != NULL, ESP_ERR_NO_MEM, err, TAG, "Failed to create recorder queue");

    ESP_GOTO_ON_ERROR(audio_recorder_init_wakenet(rec), err, TAG, "Failed to init WakeNet");

    uint32_t board_chunk_samples = (rec->wn_frame_samples * audio_manager.config.board_sample_rate + AUDIO_LIGHT_WN_RATE - 1) / AUDIO_LIGHT_WN_RATE;
    rec->input_buf_size = (size_t)board_chunk_samples * audio_manager.config.board_channels * audio_bytes_per_sample(audio_manager.config.board_bits);
    rec->input_buf = calloc(1, rec->input_buf_size);
    ESP_GOTO_ON_FALSE(rec->input_buf != NULL, ESP_ERR_NO_MEM, err, TAG, "No memory for recorder input");

    ESP_GOTO_ON_ERROR(audio_convert_init(&rec->to_wn_cvt,
                                         audio_manager.config.board_sample_rate, audio_manager.config.board_bits, audio_manager.config.board_channels,
                                         AUDIO_LIGHT_WN_RATE, AUDIO_LIGHT_WN_BITS, AUDIO_LIGHT_WN_CH,
                                         board_chunk_samples),
                      err, TAG, "Failed to init recorder WakeNet converter");

    const av_processor_audio_info_t *enc_info = audio_encoder_info_from_config(&rec->config.encoder_cfg);
    ESP_GOTO_ON_FALSE(enc_info != NULL, ESP_ERR_INVALID_ARG, err, TAG, "Invalid encoder info");
    ESP_GOTO_ON_ERROR(audio_convert_init(&rec->to_enc_cvt,
                                         AUDIO_LIGHT_WN_RATE, AUDIO_LIGHT_WN_BITS, AUDIO_LIGHT_WN_CH,
                                         enc_info->sample_rate, enc_info->sample_bits, enc_info->channels,
                                         rec->wn_frame_samples),
                      err, TAG, "Failed to init recorder encoder converter");
    ESP_GOTO_ON_ERROR(audio_recorder_init_encoder(rec), err, TAG, "Failed to init recorder encoder");
    ESP_GOTO_ON_ERROR(audio_worker_ensure_started(&config->recorder_task_config), err, TAG, "Failed to start worker");

    xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
    audio_manager.recorder = rec;
    xSemaphoreGive(audio_manager.lock);
    rec->state = AUDIO_RUN_STATE_PLAYING;
    *handle = rec;
    audio_worker_notify();
    ESP_LOGI(TAG, "Light recorder opened");
    return ESP_OK;

err:
    if (rec) {
        if (rec->encoder) {
            esp_audio_enc_close(rec->encoder);
        }
        if (rec->wn_model && rec->wn_iface) {
            rec->wn_iface->destroy(rec->wn_model);
        }
        if (rec->models) {
            esp_srmodel_deinit(rec->models);
        }
        audio_convert_deinit(&rec->to_wn_cvt);
        audio_convert_deinit(&rec->to_enc_cvt);
        if (rec->out_queue) {
            audio_light_packet_queue_drain(rec->out_queue);
            vQueueDelete(rec->out_queue);
        }
        SAFE_FREE(rec->input_buf);
        SAFE_FREE(rec->wn_cache);
        SAFE_FREE(rec->enc_pcm_cache);
        SAFE_FREE(rec->enc_out_buf);
        SAFE_FREE(rec);
    }
    return ret;
}

esp_err_t audio_recorder_pause(audio_recorder_handle_t rec)
{
    ESP_RETURN_ON_FALSE(rec != NULL && rec->state != AUDIO_RUN_STATE_CLOSED, ESP_FAIL, TAG, "Recorder not opened");
    rec->state = AUDIO_RUN_STATE_PAUSED;
    return ESP_OK;
}

esp_err_t audio_recorder_resume(audio_recorder_handle_t rec)
{
    ESP_RETURN_ON_FALSE(rec != NULL && rec->state != AUDIO_RUN_STATE_CLOSED, ESP_FAIL, TAG, "Recorder not opened");
    rec->state = AUDIO_RUN_STATE_PLAYING;
    audio_worker_notify();
    return ESP_OK;
}

esp_err_t audio_recorder_get_afe_manager_handle(audio_recorder_handle_t handle, esp_gmf_afe_manager_handle_t *afe_manager_handle)
{
    (void)handle;
    (void)afe_manager_handle;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t audio_recorder_get_afe_element_handle(audio_recorder_handle_t handle, esp_gmf_element_handle_t *afe_element_handle)
{
    (void)handle;
    (void)afe_element_handle;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t audio_recorder_close(audio_recorder_handle_t rec)
{
    if (!rec) {
        return ESP_OK;
    }
    if (audio_manager.lock) {
        xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
        if (audio_manager.recorder == rec) {
            audio_manager.recorder = NULL;
        }
        xSemaphoreGive(audio_manager.lock);
    }
    rec->state = AUDIO_RUN_STATE_CLOSED;
    while (audio_manager.lock) {
        bool busy = false;
        xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
        busy = audio_manager.recorder_busy;
        xSemaphoreGive(audio_manager.lock);
        if (!busy) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(AUDIO_LIGHT_CLOSE_WAIT_MS));
    }
    if (rec->encoder) {
        esp_audio_enc_close(rec->encoder);
        rec->encoder = NULL;
    }
    if (rec->wn_model && rec->wn_iface) {
        rec->wn_iface->destroy(rec->wn_model);
        rec->wn_model = NULL;
    }
    if (rec->models) {
        esp_srmodel_deinit(rec->models);
        rec->models = NULL;
    }
    audio_convert_deinit(&rec->to_wn_cvt);
    audio_convert_deinit(&rec->to_enc_cvt);
    if (rec->out_queue) {
        audio_light_packet_queue_drain(rec->out_queue);
        vQueueDelete(rec->out_queue);
        rec->out_queue = NULL;
    }
    audio_light_packet_free(rec->pending_pkt);
    SAFE_FREE(rec->input_buf);
    SAFE_FREE(rec->wn_cache);
    SAFE_FREE(rec->enc_pcm_cache);
    SAFE_FREE(rec->enc_out_buf);
    SAFE_FREE(rec);
    audio_worker_request_stop_if_idle();
    return ESP_OK;
}

esp_err_t audio_recorder_read_data(audio_recorder_handle_t rec, uint8_t *data, int data_size)
{
    ESP_RETURN_ON_FALSE(rec != NULL && data != NULL && data_size > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid recorder read");
    ESP_RETURN_ON_FALSE(rec->state != AUDIO_RUN_STATE_CLOSED, ESP_FAIL, TAG, "Recorder closed");

    if (rec->pending_pkt == NULL) {
        if (xQueueReceive(rec->out_queue, &rec->pending_pkt, portMAX_DELAY) != pdTRUE) {
            return ESP_FAIL;
        }
        rec->pending_offset = 0;
    }

    size_t remain = rec->pending_pkt->len - rec->pending_offset;
    size_t to_copy = remain > (size_t)data_size ? (size_t)data_size : remain;
    memcpy(data, rec->pending_pkt->data + rec->pending_offset, to_copy);
    rec->pending_offset += to_copy;
    if (rec->pending_offset >= rec->pending_pkt->len) {
        rec->pending_offset = 0;
        audio_light_packet_free(rec->pending_pkt);
        rec->pending_pkt = NULL;
    }
    return (esp_err_t)to_copy;
}

esp_err_t audio_feeder_open(audio_feeder_config_t *config, audio_feeder_handle_t *handle)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(config != NULL && handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid feeder config");
    *handle = NULL;
    ESP_RETURN_ON_FALSE(audio_manager.feeder == NULL, ESP_ERR_INVALID_STATE, TAG, "Only one light feeder is supported");
    ESP_RETURN_ON_FALSE(config->decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_PCM ||
                        config->decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711A ||
                        config->decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711U ||
                        config->decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_OPUS,
                        ESP_ERR_NOT_SUPPORTED, TAG, "Light feeder supports PCM/G711/OPUS only");

    audio_feeder_handle_t fd = calloc(1, sizeof(*fd));
    ESP_RETURN_ON_FALSE(fd != NULL, ESP_ERR_NO_MEM, TAG, "No memory for feeder");
    fd->state = AUDIO_RUN_STATE_CLOSED;
    fd->config = *config;
    fd->in_queue = xQueueCreate(AUDIO_LIGHT_FEED_QUEUE_LEN, sizeof(audio_light_packet_t *));
    ESP_GOTO_ON_FALSE(fd->in_queue != NULL, ESP_ERR_NO_MEM, err, TAG, "Failed to create feeder queue");

    const av_processor_audio_info_t *dec_info = audio_decoder_info_from_config(&fd->config.decoder_cfg);
    ESP_GOTO_ON_FALSE(dec_info != NULL, ESP_ERR_INVALID_ARG, err, TAG, "Invalid feeder decoder info");

    if (fd->config.decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711A ||
        fd->config.decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711U ||
        fd->config.decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_OPUS) {
        esp_audio_dec_cfg_t dec_cfg = {
            .type = audio_format_to_codec_type(fd->config.decoder_cfg.format),
            .cfg = NULL,
            .cfg_sz = 0,
        };
        esp_g711_dec_cfg_t g711_cfg = ESP_G711_DEC_CONFIG_DEFAULT();
        esp_opus_dec_cfg_t opus_cfg = ESP_OPUS_DEC_CONFIG_DEFAULT();
        if (fd->config.decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711A ||
            fd->config.decoder_cfg.format == AV_PROCESSOR_FORMAT_ID_G711U) {
            g711_cfg.channel = dec_info->channels;
            dec_cfg.cfg = &g711_cfg;
            dec_cfg.cfg_sz = sizeof(g711_cfg);
        } else {
            opus_cfg.sample_rate = dec_info->sample_rate;
            opus_cfg.channel = dec_info->channels;
            opus_cfg.frame_duration = audio_map_opus_dec_frame_duration(dec_info->frame_duration);
            opus_cfg.self_delimited = false;
            dec_cfg.cfg = &opus_cfg;
            dec_cfg.cfg_sz = sizeof(opus_cfg);
        }
        ESP_GOTO_ON_FALSE(esp_audio_dec_open(&dec_cfg, &fd->decoder) == ESP_AUDIO_ERR_OK,
                          ESP_FAIL, err, TAG, "Failed to open feeder decoder");
    }

    uint32_t max_samples = dec_info->sample_rate * (dec_info->frame_duration ? dec_info->frame_duration : 20) / 1000;
    if (max_samples == 0) {
        max_samples = dec_info->sample_rate / 50;
    }
    fd->dec_out_buf_size = (size_t)max_samples * dec_info->channels * audio_bytes_per_sample(dec_info->sample_bits ? dec_info->sample_bits : 16);
    if (fd->dec_out_buf_size == 0) {
        fd->dec_out_buf_size = AUDIO_LIGHT_PACKET_MAX * 4;
    }
    fd->dec_out_buf = calloc(1, fd->dec_out_buf_size);
    ESP_GOTO_ON_FALSE(fd->dec_out_buf != NULL, ESP_ERR_NO_MEM, err, TAG, "No memory for feeder decode output");

    ESP_GOTO_ON_ERROR(audio_convert_init(&fd->to_board_cvt,
                                         dec_info->sample_rate, dec_info->sample_bits, dec_info->channels,
                                         audio_manager.config.board_sample_rate, audio_manager.config.board_bits, audio_manager.config.board_channels,
                                         max_samples),
                      err, TAG, "Failed to init feeder board converter");
    ESP_GOTO_ON_ERROR(audio_worker_ensure_started(&config->feeder_task_config), err, TAG, "Failed to start worker");

    xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
    audio_manager.feeder = fd;
    xSemaphoreGive(audio_manager.lock);
    fd->state = AUDIO_RUN_STATE_IDLE;
    *handle = fd;
    ESP_LOGI(TAG, "Light feeder opened");
    return ESP_OK;

err:
    if (fd) {
        if (fd->decoder) {
            esp_audio_dec_close(fd->decoder);
        }
        audio_convert_deinit(&fd->to_board_cvt);
        if (fd->in_queue) {
            audio_light_packet_queue_drain(fd->in_queue);
            vQueueDelete(fd->in_queue);
        }
        SAFE_FREE(fd->dec_out_buf);
        SAFE_FREE(fd);
    }
    return ret;
}

esp_err_t audio_feeder_close(audio_feeder_handle_t fd)
{
    if (!fd) {
        return ESP_OK;
    }
    if (audio_manager.lock) {
        xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
        if (audio_manager.feeder == fd) {
            audio_manager.feeder = NULL;
        }
        xSemaphoreGive(audio_manager.lock);
    }
    fd->state = AUDIO_RUN_STATE_CLOSED;
    while (audio_manager.lock) {
        bool busy = false;
        xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
        busy = audio_manager.feeder_busy;
        xSemaphoreGive(audio_manager.lock);
        if (!busy) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(AUDIO_LIGHT_CLOSE_WAIT_MS));
    }
    if (fd->decoder) {
        esp_audio_dec_close(fd->decoder);
        fd->decoder = NULL;
    }
    audio_convert_deinit(&fd->to_board_cvt);
    if (fd->in_queue) {
        audio_light_packet_queue_drain(fd->in_queue);
        vQueueDelete(fd->in_queue);
        fd->in_queue = NULL;
    }
    SAFE_FREE(fd->dec_out_buf);
    SAFE_FREE(fd);
    audio_worker_request_stop_if_idle();
    return ESP_OK;
}

esp_err_t audio_feeder_start(audio_feeder_handle_t fd)
{
    ESP_RETURN_ON_FALSE(fd != NULL && fd->state != AUDIO_RUN_STATE_CLOSED, ESP_FAIL, TAG, "Feeder not opened");
    fd->state = AUDIO_RUN_STATE_PLAYING;
    audio_worker_notify();
    return ESP_OK;
}

esp_err_t audio_feeder_stop(audio_feeder_handle_t fd)
{
    ESP_RETURN_ON_FALSE(fd != NULL && fd->state != AUDIO_RUN_STATE_CLOSED, ESP_FAIL, TAG, "Feeder not opened");
    fd->state = AUDIO_RUN_STATE_IDLE;
    if (fd->in_queue) {
        audio_light_packet_queue_drain(fd->in_queue);
        xQueueReset(fd->in_queue);
    }
    if (fd->decoder) {
        (void)esp_audio_dec_reset(fd->decoder);
    }
    return ESP_OK;
}

esp_err_t audio_feeder_feed_data(audio_feeder_handle_t fd, uint8_t *data, int data_size)
{
    ESP_RETURN_ON_FALSE(fd != NULL && data != NULL && data_size > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid feeder data");
    ESP_RETURN_ON_FALSE(fd->state != AUDIO_RUN_STATE_CLOSED, ESP_FAIL, TAG, "Feeder closed");

    if (fd->config.decoder_cfg.format != AV_PROCESSOR_FORMAT_ID_PCM) {
        ESP_RETURN_ON_FALSE(data_size <= AUDIO_LIGHT_PACKET_MAX, ESP_ERR_INVALID_SIZE, TAG, "Encoded feeder packet too large");
        audio_light_packet_t *pkt = audio_light_packet_alloc(data, data_size);
        ESP_RETURN_ON_FALSE(pkt != NULL, ESP_ERR_NO_MEM, TAG, "Failed to alloc feeder packet");
        if (xQueueSend(fd->in_queue, &pkt, portMAX_DELAY) != pdTRUE) {
            audio_light_packet_free(pkt);
            return ESP_FAIL;
        }
    } else {
        int offset = 0;
        while (offset < data_size) {
            int chunk = data_size - offset;
            if (chunk > AUDIO_LIGHT_PACKET_MAX) {
                chunk = AUDIO_LIGHT_PACKET_MAX;
            }
            audio_light_packet_t *pkt = audio_light_packet_alloc(data + offset, chunk);
            ESP_RETURN_ON_FALSE(pkt != NULL, ESP_ERR_NO_MEM, TAG, "Failed to alloc feeder PCM");
            if (xQueueSend(fd->in_queue, &pkt, portMAX_DELAY) != pdTRUE) {
                audio_light_packet_free(pkt);
                return ESP_FAIL;
            }
            offset += chunk;
        }
    }
    audio_worker_notify();
    return ESP_OK;
}

esp_err_t audio_processor_mixer_open(audio_play_handle_t playback, audio_feeder_handle_t feeder)
{
    (void)playback;
    (void)feeder;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t audio_processor_mixer_close(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t audio_processor_ramp_control(audio_mixer_focus_mode_t mode)
{
    (void)mode;
    return ESP_ERR_NOT_SUPPORTED;
}
