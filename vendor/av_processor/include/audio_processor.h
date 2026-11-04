/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#include "esp_err.h"
#include "esp_gmf_afe_manager.h"
#include "esp_audio_simple_player_advance.h"
#include "av_processor_type.h"

typedef struct audio_recorder_s *audio_recorder_handle_t;
typedef struct audio_play_s     *audio_play_handle_t;
typedef struct audio_feeder_s   *audio_feeder_handle_t;

/* Brief:  Default audio manager configuration
 *  Note: When use OPUS decoder or OPUS encoder, the recorder_task_config and feeder_task_config
 *  task stack size need be set to more than 4096 * 10 bytes.
 */
#define DEFAULT_AUDIO_MANAGER_CONFIG() {  \
    .play_io           = {0},             \
    .rec_io            = {0},             \
    .mic_layout        = {0},             \
    .board_sample_rate = 16000,           \
    .board_bits        = 32,              \
    .board_channels    = 2,               \
}

/**
 * @brief  Audio input callback used by `audio_io_ops_t`
 *
 * @param[in]   data       Destination buffer to fill with audio data
 * @param[in]   data_size  Number of bytes requested
 * @param[in]   ctx        User-defined callback context
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
typedef esp_err_t (*audio_io_read_cb_t)(uint8_t *data, int data_size, void *ctx);

/**
 * @brief  Audio output callback used by `audio_io_ops_t`
 *
 * @param[in]   data       Audio data to be written out
 * @param[in]   data_size  Number of bytes to write
 * @param[in]   ctx        User-defined callback context
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
typedef esp_err_t (*audio_io_write_cb_t)(uint8_t *data, int data_size, void *ctx);

/**
 * @brief  Generic audio input/output callback set
 *
 *         This abstraction is not limited to physical audio devices. It may be
 *         backed by codec hardware, USB, network transport, file storage, SD
 *         card, memory buffers, or any user-defined audio endpoint.
 */
typedef struct {
    audio_io_read_cb_t   read_cb;    /*!< Optional audio input callback */
    void                *read_ctx;   /*!< User context for `read_cb` */
    audio_io_write_cb_t  write_cb;   /*!< Optional audio output callback */
    void                *write_ctx;  /*!< User context for `write_cb` */
} audio_io_ops_t;

#define DEFAULT_AUDIO_FEEDER_CONFIG() {  \
    .feeder_task_config = {              \
        .task_stack        = 0,          \
        .task_prio         = 0,          \
        .task_core         = 0,          \
        .task_stack_in_ext = true        \
    },                                   \
    .feeder_mixer_gain = {               \
        .initial_gain = 0.9,             \
        .target_gain = 1.0,              \
        .transition_time = 1500          \
    },                                   \
}

#define DEFAULT_AUDIO_PLAY_CONFIG() {             \
    .type = AUDIO_PLAY_TYPE_PLAYBACK,             \
    .event_cb = NULL,                             \
    .event_cb_ctx = NULL,                         \
    .task_config = {                              \
        .task_stack = 4096,                       \
        .task_prio = 5,                           \
        .task_core = 0,                           \
        .task_stack_in_ext = true,                \
    },                                            \
    .mixer_gain = {                               \
        .initial_gain = 0.6,                      \
        .target_gain = 1.0,                       \
        .transition_time = 1500                   \
    },                                            \
    .block_until_done = false                     \
}

#define DEFAULT_AUDIO_PLAY_PROMPT_CONFIG() {      \
    .type = AUDIO_PLAY_TYPE_PROMPT,               \
    .event_cb = NULL,                             \
    .event_cb_ctx = NULL,                         \
    .task_config = {                              \
        .task_stack = 4096,                       \
        .task_prio = 5,                           \
        .task_core = 0,                           \
        .task_stack_in_ext = true,                \
    },                                            \
    .mixer_gain = {                               \
        .initial_gain = 1.0,                      \
        .target_gain = 1.0,                       \
        .transition_time = 0                      \
    },                                            \
    .block_until_done = true                      \
}

#define DEFAULT_AUDIO_RECORDER_CONFIG() {  \
    .afe_feed_task_config = {              \
        .task_stack        = 40 * 1024,    \
        .task_prio         = 5,            \
        .task_core         = 0,            \
        .task_stack_in_ext = true,         \
    },                                     \
    .afe_config             = DEFAULT_AV_PROCESSOR_CUSTOM_CONFIG(), \
    .afe_fetch_task_config = {             \
        .task_stack = 6 * 1024,            \
        .task_prio = 5,                    \
        .task_core = 1,                    \
        .task_stack_in_ext = true          \
    },                                     \
    .recorder_event_cb     = NULL,         \
    .recorder_ctx          = NULL,         \
    .recorder_task_config  = {             \
        .task_stack = 4096,                \
        .task_prio = 10,                   \
        .task_core = 0,                    \
        .task_stack_in_ext = true          \
    },                                     \
    .encoder_cfg           = { .format = (av_processor_format_id_t)0 }, \
}

/**
 * @brief  Default AFE configuration macro
 */
#define DEFAULT_AV_PROCESSOR_AFE_CONFIG() {  \
    .model              = "model",           \
    .frontend_type      = AV_PROCESSOR_FRONTEND_TYPE_AFE, \
    .mode = {                                \
        .afe = {                             \
            .algo_mask = 0,                  \
            .vad_mode = 4,                   \
            .vad_min_speech_ms = 64,         \
            .vad_min_noise_ms = 1000,        \
            .afe_type = AFE_TYPE_SR,         \
            .afe_mode = AFE_MODE_LOW_COST,   \
            .vcmd_timeout_ms = 5000,         \
            .mn_language = "cn",             \
            .wakeup_end_time_ms = 2000,      \
            .wakeup_time_ms = 10000,         \
        },                                   \
    },                                       \
}

/**
 * @brief  Default custom frontend configuration macro
 */
#define DEFAULT_AV_PROCESSOR_CUSTOM_CONFIG() {  \
    .model              = "model",              \
    .frontend_type      = AV_PROCESSOR_FRONTEND_TYPE_CUSTOM, \
    .mode = {                                   \
        .custom = {                             \
            .algo_mask = 0,                     \
            .vad_mode = 4,                      \
            .vad_min_speech_ms = 64,            \
            .vad_min_noise_ms = 1000,           \
        },                                      \
    },                                          \
}

/**
 * @brief  Audio focus control mode - determines which audio stream gets priority
 */
typedef enum {
    AUDIO_MIXER_FOCUS_FEEDER   = 0,  /*!< Focus on FEEDER stream - boost feeder, reduce playback */
    AUDIO_MIXER_FOCUS_PLAYBACK = 1,  /*!< Focus on PLAYBACK stream - boost playback, reduce feeder */
    AUDIO_MIXER_FOCUS_BALANCED = 2,  /*!< Balanced mode - both streams at equal levels */
} audio_mixer_focus_mode_t;

/**
 * @brief  Audio player state enumeration
 */
typedef enum {
    AUDIO_PLAY_STATE_IDLE,      /*!< Player is idle */
    AUDIO_PLAY_STATE_PLAYING,   /*!< Player is playing */
    AUDIO_PLAY_STATE_FINISHED,  /*!< Player has finished playing */
    AUDIO_PLAY_STATE_STOPPED,   /*!< Player has stopped */
    AUDIO_PLAY_STATE_PAUSED,    /*!< Player is paused */
    AUDIO_PLAY_STATE_ERROR,     /*!< Player has encountered an error */
} audio_play_state_t;

typedef enum {
    AUDIO_PLAY_TYPE_PLAYBACK = 0,  /*!< Normal playback stream, supports mixer/event callback */
    AUDIO_PLAY_TYPE_PROMPT,        /*!< Prompt stream, direct output and blocking play by default */
} audio_play_type_t;

/**
 * @brief  Type definition for the audio recorder event callback function
 *
 * @param[in]  recorder  Recorder instance handle returned by `audio_recorder_open`
 * @param[in]  event    Pointer to the event data.
 *                      When `ai_afe` is selected, this is `esp_gmf_afe_evt_t *`.
 *                      When `ai_wn` is selected, this is `audio_recorder_wn_event_t *`.
 * @param[in]  ctx      User-defined context pointer from `audio_recorder_config_t::recorder_ctx`
 */
typedef void (*recorder_event_callback_t)(audio_recorder_handle_t recorder, void *event, void *ctx);

/**
 * @brief  WakeNet event payload delivered through `recorder_event_callback_t`
 *         when the recorder frontend is `ai_wn`
 */
typedef struct {
    int32_t  trigger_ch;  /*!< Microphone channel index that triggered wake word detection */
} audio_recorder_wn_event_t;

/**
 * @brief  Type definition for the audio recorder input data callback function
 * @note   This callback is used to get the audio data from the input.
 *
 * @param[in]  data       Pointer to the audio data to be read
 * @param[in]  data_size  Size of the audio data to be read
 * @param[in]  ctx        User-defined context pointer
 */
typedef void (*recorder_input_callback_t)(uint8_t *data, int data_size, void *ctx);

/**
 * @brief  Type definition for the audio playback event callback function
 *
 * @param[in]  state  Playback state
 * @param[in]  ctx    User-defined context pointer
 */
typedef void (*playback_event_callback_t)(audio_play_state_t state, void *ctx);

typedef enum {
    AV_PROCESSOR_AFE_FLAG_AI_MODE_WAKEUP     = BIT(0),  /*!< `ai_afe` mode: WAKEUP when set, DIRECT when cleared */
    AV_PROCESSOR_AFE_FLAG_AEC_ENABLE         = BIT(1),  /*!< Enable AEC inside `ai_afe` */
    AV_PROCESSOR_AFE_FLAG_NS_ENABLE          = BIT(2),  /*!< Enable NS inside `ai_afe` */
    AV_PROCESSOR_AFE_FLAG_VAD_ENABLE         = BIT(3),  /*!< Enable VAD inside `ai_afe` */
    AV_PROCESSOR_AFE_FLAG_AGC_ENABLE         = BIT(4),  /*!< Enable AGC inside `ai_afe` */
    AV_PROCESSOR_AFE_FLAG_VCMD_DETECT_ENABLE = BIT(5),  /*!< Enable Voice Command Detection inside `ai_afe` */
} av_processor_afe_flag_t;

typedef enum {
    AV_PROCESSOR_CUSTOM_FLAG_WAKENET_ENABLE = BIT(0),  /*!< Enable standalone `ai_wn` in the custom frontend path */
} av_processor_custom_flag_t;

typedef enum {
    AV_PROCESSOR_FRONTEND_TYPE_CUSTOM = 0,  /*!< Use application-integrated/custom audio frontend path */
    AV_PROCESSOR_FRONTEND_TYPE_AFE,         /*!< Use built-in `ai_afe` frontend based on esp-sr */
} av_processor_frontend_type_t;

typedef struct {
    uint32_t  algo_mask;          /*!< Custom frontend algorithm bitmask. Currently only `AV_PROCESSOR_CUSTOM_FLAG_WAKENET_ENABLE` is supported; zero means raw path */
    int       vad_mode;           /*!< Reserved for future custom frontend expansion */
    int       vad_min_speech_ms;  /*!< Reserved for future custom frontend expansion */
    int       vad_min_noise_ms;   /*!< Reserved for future custom frontend expansion */
} av_processor_custom_mode_config_t;

typedef struct {
    uint32_t    algo_mask;          /*!< Raw algorithm bitmask for built-in `ai_afe` */
    int         vad_mode;           /*!< VAD mode value (1-4), larger values are more sensitive to human voice */
    int         vad_min_speech_ms;  /*!< Minimum speech duration in milliseconds */
    int         vad_min_noise_ms;   /*!< Minimum noise duration in milliseconds */
    afe_type_t  afe_type;           /*!< AFE type: AFE_TYPE_SR (0) or AFE_TYPE_VC (1) */
    afe_mode_t  afe_mode;           /*!< AFE mode: AFE_MODE_LOW_COST (0) or AFE_MODE_HIGH_PERF (1) */
    int         vcmd_timeout_ms;    /*!< Voice command detection timeout in milliseconds */
    const char *mn_language;        /*!< Model language (e.g., "cn", "en") */
    int         wakeup_end_time_ms; /*!< Wakeup end time in milliseconds */
    int         wakeup_time_ms;     /*!< Wakeup time in milliseconds */
} av_processor_afe_mode_config_t;

typedef union {
    av_processor_afe_mode_config_t     afe;      /*!< Valid when `frontend_type` is `AV_PROCESSOR_FRONTEND_TYPE_AFE` */
    av_processor_custom_mode_config_t  custom;   /*!< Valid when `frontend_type` is `AV_PROCESSOR_FRONTEND_TYPE_CUSTOM` */
} av_processor_frontend_mode_union_t;

/**
 * @brief  AI audio frontend configuration structure
 *
 *         `frontend_type` selects whether the recorder uses the built-in `ai_afe` frontend
 *         or the application-integrated custom path.
 *
 *         When `frontend_type` is `AV_PROCESSOR_FRONTEND_TYPE_AFE`, use `mode.afe.algo_mask`
 *         with `av_processor_afe_flag_t`
 *         to control internal AFE capabilities such as wakeup mode, AEC, NS, VAD, AGC, and
 *         voice command detection.
 *
 *         When `frontend_type` is `AV_PROCESSOR_FRONTEND_TYPE_CUSTOM`, use `mode.custom.algo_mask` with
 *         `av_processor_custom_flag_t`. The current built-in custom frontend support only enables
 *         standalone `ai_wn`. If the mask is zero, the recorder uses the raw path.
 */
typedef struct {
    const char                        *model;          /*!< Model path/name for SR model initialization */
    av_processor_frontend_type_t       frontend_type;  /*!< Select built-in `ai_afe` or custom frontend path */
    av_processor_frontend_mode_union_t mode;           /*!< Mode-specific function mask and parameters */
} av_processor_afe_config_t;

/* Brief:  Audio mixer gain configuration */
typedef struct {
    float  initial_gain;     /*!< Initial gain value */
    float  target_gain;      /*!< Target gain value */
    int    transition_time;  /*!< Transition time in milliseconds */
} audio_mixer_gain_config_t;

/* Brief:  Common task configuration */
typedef struct {
    int   task_stack;         /*!< Task stack size (0 means use default) */
    int   task_prio;          /*!< Task priority */
    int   task_core;          /*!< Task core ID */
    bool  task_stack_in_ext;  /*!< Whether to allocate task stack in external memory */
} audio_task_config_t;

typedef struct {
    audio_task_config_t            feeder_task_config;  /*!< Feeder task configuration */
    av_processor_decoder_config_t  decoder_cfg;         /*!< Decoder configuration */
    audio_mixer_gain_config_t      feeder_mixer_gain;   /*!< Feeder mixer gain configuration */
} audio_feeder_config_t;

typedef struct {
    audio_play_type_t          type;              /*!< Player type */
    playback_event_callback_t  event_cb;          /*!< Playback event callback, only used by playback type */
    void                      *event_cb_ctx;      /*!< Playback event context */
    audio_task_config_t        task_config;       /*!< Player task configuration */
    audio_mixer_gain_config_t  mixer_gain;        /*!< Mixer gain configuration, only used by playback type */
    bool                       block_until_done;  /*!< Use blocking play mode by default */
} audio_play_config_t;

typedef struct {
    /* only supported when CONFIG_AUDIO_AFE_ENABLE is defined */
    audio_task_config_t            afe_feed_task_config;   /*!< AFE feed task configuration */
    av_processor_afe_config_t      afe_config;             /*!< AFE configuration */
    audio_task_config_t            afe_fetch_task_config;  /*!< AFE fetch task configuration */
    recorder_event_callback_t      recorder_event_cb;      /*!< Recorder event callback */
    void                          *recorder_ctx;           /*!< Recorder context */
    
    audio_task_config_t            recorder_task_config;   /*!< Recorder task configuration */
    av_processor_encoder_config_t  encoder_cfg;            /*!< Encoder configuration */
} audio_recorder_config_t;

/* Brief:  Audio manager configuration */
typedef struct {
    audio_io_ops_t   play_io;            /*!< Playback I/O callbacks */
    audio_io_ops_t   rec_io;             /*!< Record I/O callbacks */
    char             mic_layout[8];
    int              board_sample_rate;
    int              board_bits;
    int              board_channels;
} audio_manager_config_t;

/**
 * @brief  Initializes the audio manager module.
 *
 * @param[in]  config  Pointer to the audio manager configuration structure
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_manager_init(audio_manager_config_t *config);

/**
 * @brief  Deinitializes the audio manager component
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_manager_deinit();

/**
 * @brief  Opens the audio recorder and registers an event callback
 *
 * @param[in]   config  Pointer to the audio recorder configuration structure.
 * @param[out]  handle  Pointer to receive the recorder handle on success.
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_recorder_open(audio_recorder_config_t *config, audio_recorder_handle_t *handle);

/**
 * @brief  Pauses the audio recorder pipeline
 *
 * @param[in]  handle  Recorder handle returned by `audio_recorder_open`
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_recorder_pause(audio_recorder_handle_t handle);

/**
 * @brief  Resumes the audio recorder pipeline
 *
 * @param[in]  handle  Recorder handle returned by `audio_recorder_open`
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_recorder_resume(audio_recorder_handle_t handle);

/**
 * @brief  Get the AFE manager handle of the audio recorder
 * @note   This function is used to get the AFE manager handle of the audio recorder.
 *         When you get the AFE manager handle, you can use the AFE manager API to configure the AFE.
 *         For example:
 *         ```c
 *         audio_recorder_handle_t rec;
 *         esp_gmf_afe_manager_handle_t afe_manager_handle;
 *         audio_recorder_get_afe_manager_handle(rec, &afe_manager_handle);
 *         esp_gmf_afe_manager_suspend(afe_manager_handle, true);
 *         ```
 *
 * @param[in]   handle              Recorder handle returned by `audio_recorder_open`
 * @param[out]  afe_manager_handle  Pointer to the AFE manager handle
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_recorder_get_afe_manager_handle(audio_recorder_handle_t handle, esp_gmf_afe_manager_handle_t *afe_manager_handle);

/* @brief  Get the AFE element handle of the audio recorder
 * @note   This function is used to get the AFE element handle of the audio recorder.
 *         When you get the AFE element handle, you can call all APIs in esp_gmf_afe.h to configure the AFE.
 *         For example:
 *         ```c
 *         audio_recorder_handle_t rec;
 *         esp_gmf_element_handle_t afe_element_handle;
 *         audio_recorder_get_afe_element_handle(rec, &afe_element_handle);
 *         esp_gmf_afe_trigger_wakeup(afe_element_handle);
 *         ```
 *
 * @param[in]   handle              Recorder handle returned by `audio_recorder_open`
 * @param[out]  afe_element_handle  Pointer to the AFE element handle
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_recorder_get_afe_element_handle(audio_recorder_handle_t handle, esp_gmf_element_handle_t *afe_element_handle);

/**
 * @brief  Closes the audio recorder and frees the handle
 *
 * @param[in]  handle  Recorder handle returned by `audio_recorder_open`
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_recorder_close(audio_recorder_handle_t handle);

/**
 * @brief  Reads audio data from the recorder.
 *
 * @param[in]   handle     Recorder handle returned by `audio_recorder_open`
 * @param[out]  data       Pointer to the buffer where the audio data will be stored
 * @param[in]   data_size  Size of the buffer to store the audio data.
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_recorder_read_data(audio_recorder_handle_t handle, uint8_t *data, int data_size);

/**
 * @brief  Opens the audio feeder system
 *
 * @param[in]  config  Pointer to the audio feeder configuration structure.
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_feeder_open(audio_feeder_config_t *config, audio_feeder_handle_t *handle);

/**
 * @brief  Closes the audio feeder module
 *
 * @param[in]  handle  Feeder handle returned by `audio_feeder_open`
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_feeder_close(audio_feeder_handle_t handle);

/**
 * @brief  Starts the audio feeder operation.
 *
 * @param[in]  handle  Feeder handle returned by `audio_feeder_open`
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_feeder_start(audio_feeder_handle_t handle);

/**
 * @brief  Stops the ongoing audio feeder
 *
 * @param[in]  handle  Feeder handle returned by `audio_feeder_open`
 *
 * @return
 *       - ESP_OK  On successfully stopping the audio playback
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_feeder_stop(audio_feeder_handle_t handle);

/**
 * @brief  Feeds audio data into the feeder system
 *
 * @param[in]  handle     Feeder handle returned by `audio_feeder_open`
 * @param[in]  data       Pointer to the audio data to be fed into the feeder system
 * @param[in]  data_size  Size of the audio data to be fed into the feeder system
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_feeder_feed_data(audio_feeder_handle_t handle, uint8_t *data, int data_size);

/**
 * @brief  Open the audio play module
 *
 * @param[in]   config  Pointer to the audio play configuration structure.
 * @param[out]  handle  Pointer to receive the play handle on success.
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_playback_open(audio_play_config_t *config, audio_play_handle_t *handle);

/**
 * @brief  Close the audio play module
 *
 * @param[in]  handle  Play handle returned by `audio_playback_open`
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_playback_close(audio_play_handle_t handle);

/**
 * @brief  Play audio from the specified URL.
 *
 * @param[in]  handle  Play handle returned by `audio_playback_open`
 * @param[in]  url     URL pointing to the audio file to be played
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_playback_play(audio_play_handle_t handle, const char *url);

/**
 * @brief  Pause the audio play
 *
 * @param[in]  handle  Play handle returned by `audio_playback_open`
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_playback_pause(audio_play_handle_t handle);

/**
 * @brief  Resume the audio play
 *
 * @param[in]  handle  Play handle returned by `audio_playback_open`
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_playback_resume(audio_play_handle_t handle);

/**
 * @brief  Stop the currently playing audio
 *
 * @param[in]  handle  Play handle returned by `audio_playback_open`
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_playback_stop(audio_play_handle_t handle);

/**
 * @brief  Open the audio mixer module
 *
 * @param[in]  playback  Play handle returned by `audio_playback_open`
 * @param[in]  feeder    Feeder handle returned by `audio_feeder_open`
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_processor_mixer_open(audio_play_handle_t playback, audio_feeder_handle_t feeder);

/**
 * @brief  Close the audio mixer module
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   Appropriate esp_err_t error code on failure
 */
esp_err_t audio_processor_mixer_close(void);

/**
 * @brief  Control audio focus between FEEDER and PLAYBACK streams
 *
 * @note  This function controls the fade processors for both feeder and playback streams
 *        to create smooth volume transitions and focus on the desired audio source.
 *        This only works when the audio mixer is enabled.
 *
 * @param[in]  mode  The focus control mode:
 *                   - AUDIO_MIXER_FOCUS_FEEDER: Focus on FEEDER stream (boost feeder, reduce playback)
 *                   - AUDIO_MIXER_FOCUS_PLAYBACK: Focus on PLAYBACK stream (boost playback, reduce feeder)
 *                   - AUDIO_MIXER_FOCUS_BALANCED: Balanced mode (both streams at equal levels)
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid mode parameter
 *       - ESP_ERR_INVALID_STATE  Audio render not initialized or streams not available
 *       - ESP_FAIL               Failed to control fade processors
 */
esp_err_t audio_processor_ramp_control(audio_mixer_focus_mode_t mode);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
