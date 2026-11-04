/**
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2026 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
/*
 * Doubao Realtime 3.0 (Seeduplex) WebSocket client - Volcano Ark.
 *
 * Replaces the original OpenAI Realtime WebRTC (calls/DTLS) transport.
 *
 * Protocol (docs.volcengine.com/docs/6561/2549778):
 *   endpoint: wss://openspeech.bytedance.com/api/v3/duplex/realtime/dialogue
 *   auth:     single header X-Api-Key (from console > API Key management)
 *   frames:   JSON text frames
 *     up:   session.create (model fixed 1.2.6.1), input_audio_buffer.append (base64),
 *           input_audio_buffer.commit, input_audio_mute.commit / input_audio_unmute.commit,
 *           response.cancel, session.close
 *     down: session.created, response.output_audio.delta (base64 pcm_s16le 24k), ...done
 *   up audio:  speech_opus 16k mono (brookesia encoder output matches)
 *   down audio: pcm_s16le 24k mono (resampled to 16k in agent layer)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_websocket_client.h"
#include "esp_peer.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "mbedtls/base64.h"
#include "cJSON.h"
#include "openai.h"

static const char *TAG = "openai_doubao";

/* ---------------- 豆包 Realtime 3.0 常量 ---------------- */
#define DOUBAO_WS_URI       "wss://openspeech.bytedance.com/api/v3/duplex/realtime/dialogue"
#define DOUBAO_MODEL        "1.2.6.1"
#define DOUBAO_DEF_VOICE    "zh_female_vv_uranus_bigtts"   /* Vivi 2.0 官方主推音色（2026-10-29；v17 试 jupiter 连接成功但不回话，2026-11-03 回退） */

/* 下行事件 */
#define EV_SESSION_CREATED      "session.created"
#define EV_SESSION_CLOSED       "session.closed"
#define EV_OUT_AUDIO_DELTA      "response.output_audio.delta"
#define EV_OUT_AUDIO_DONE       "response.output_audio.done"
#define EV_OUT_AUDIO_STARTED    "response.output_audio.started"
#define EV_ERR                  "error"

typedef struct {
    openai_config_t cfg;
    esp_websocket_client_handle_t ws;
    bool ws_connected;
    bool session_started;
    bool agent_started;
    bool initialized;
    bool ever_connected;    /* LOCAL FIX (2026-10-29): 是否曾经握手成功 */
    bool auto_input_stopped; /* LOCAL FIX (2026-11-02): 服务端响应后已自动停止输入 */
    uint32_t last_commit_ms;  /* LOCAL FIX (2026-11-03): commit 发送时间（确认超时检测，esp_timer ms） */
    uint8_t commit_pending;   /* 是否有未确认的 commit */
    uint8_t reconnect_pending; /* LOCAL FIX (2026-11-03): 会话被服务端关闭，待 watchdog 任务重连 */
    TaskHandle_t commit_wd;   /* commit 确认看门狗任务 */
    SemaphoreHandle_t lock;
} doubao_ctx_t;

static doubao_ctx_t s_ctx;

/* ---------------- 发送 ---------------- */

/* LOCAL FIX (2026-11-03): commit 确认看门狗——commit 后服务端无 committed 回显
 * 说明连接已死（esp_websocket_client 写失败假成功 + 读错误不报断事件，永久卡死）。
 * 用 FreeRTOS 任务（非 esp_timer 回调：stop 阻塞等 WS 任务退出，esp_timer 上下文
 * 会卡住导致重连不执行），独立 1s 周期检查、不依赖下次按键，
 * 8s 无确认自动 stop+start 重连，重连后 session.create -> 框架 listening 状态复位，
 * 下次按键即可继续说话。 */
static void commit_watchdog_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "commit watchdog task started");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        /* LOCAL FIX (2026-11-03): 不做手动 stop+start——manual54 实证 watchdog 任务
         * 上下文调 stop 触发 Interrupt wdt timeout / LoadProhibited 崩溃。
         * 服务端关连接后 esp_websocket_client 自动重连安全（manual54 91796 实证），
         * 连接假死时靠用户下次按键写失败 -> ws_send_json 重连分支恢复。 */
        if (s_ctx.commit_pending && s_ctx.last_commit_ms != 0) {
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            if (now_ms - s_ctx.last_commit_ms > 7000) {
                ESP_LOGW(TAG, "commit ack timeout (%lu ms), session stale (watchdog)",
                         (unsigned long)(now_ms - s_ctx.last_commit_ms));
                s_ctx.commit_pending = 0;
            }
        }
    }
}

static esp_err_t ws_send_json(const cJSON *root)
{
    /* LOCAL FIX (2026-11-03): 双保险——发送前检查 commit 确认超时（只清标志，
     * 不做手动 stop+start——manual54 实证手动 stop 触发崩溃，依赖自动重连/写失败分支） */
    if (s_ctx.commit_pending && s_ctx.last_commit_ms != 0 &&
        ((uint32_t)(esp_timer_get_time() / 1000) - s_ctx.last_commit_ms) > 7000) {
        ESP_LOGW(TAG, "commit ack timeout (last=%lu), session stale", (unsigned long)s_ctx.last_commit_ms);
        s_ctx.commit_pending = 0;
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ctx.ws == NULL || !s_ctx.ws_connected) {
        ESP_LOGW(TAG, "ws not connected, drop json event");
        return ESP_ERR_INVALID_STATE;
    }
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "SEND JSON: %s", json_str);   /* LOCAL DEBUG (2026-10-30) */
    int ret = esp_websocket_client_send_text(s_ctx.ws, json_str, (int)strlen(json_str), pdMS_TO_TICKS(2000));
    cJSON_free(json_str);
    if (ret < 0) {
        ESP_LOGE(TAG, "ws send failed: %d", ret);
        /* LOCAL FIX (2026-11-03): 写失败 = 服务端响应后静默关闭连接（~20-38s），
         * 客户端不知情导致后续按键写大包失败、连接卡死。
         * 主动 stop+start 重连：stop 触发 DISCONNECTED（运行中断链不通知上层），
         * start 后 CONNECTED 重发 session.create 恢复会话，按键可继续说话。 */
        if (s_ctx.ws != NULL) {
            esp_websocket_client_stop(s_ctx.ws);
            esp_websocket_client_start(s_ctx.ws);
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t send_session_create(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *session = cJSON_CreateObject();
    cJSON *audio = cJSON_CreateObject();
    cJSON *input = cJSON_CreateObject();
    cJSON *output = cJSON_CreateObject();
    if (root == NULL || session == NULL || audio == NULL || input == NULL || output == NULL) {
        if (root) cJSON_Delete(root);
        if (session) cJSON_Delete(session);
        if (audio) cJSON_Delete(audio);
        if (input) cJSON_Delete(input);
        if (output) cJSON_Delete(output);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "session.create");

    /* 输入：speech_opus 16k（与固件 encoder 匹配） */
    cJSON *in_fmt = cJSON_CreateObject();
    cJSON_AddStringToObject(in_fmt, "type", "speech_opus");
    cJSON_AddNumberToObject(in_fmt, "rate", 16000);
    cJSON_AddItemToObject(input, "format", in_fmt);
    cJSON_AddItemToObject(audio, "input", input);

    /* 输出：pcm_s16le 24k + 音色 */
    cJSON *out_fmt = cJSON_CreateObject();
    cJSON_AddStringToObject(out_fmt, "type", "pcm_s16le");
    cJSON_AddNumberToObject(out_fmt, "rate", 24000);
    cJSON_AddItemToObject(output, "format", out_fmt);
    const char *voice = (s_ctx.cfg.voice != NULL && s_ctx.cfg.voice[0] != '\0')
                        ? s_ctx.cfg.voice : DOUBAO_DEF_VOICE;
    if (strcmp(voice, "alloy") == 0 || strcmp(voice, "echo") == 0 ||
        strcmp(voice, "shimmer") == 0 || strcmp(voice, "coral") == 0) {
        voice = DOUBAO_DEF_VOICE;
    }
    cJSON_AddStringToObject(output, "voice", voice);
    cJSON_AddNumberToObject(output, "loudness", 15);   /* LOCAL FIX (2026-11-01): 输出音量 +15，缓解"声音小"（官方 loudness 范围 -50~100，100=2.0 倍） */
    cJSON_AddItemToObject(audio, "output", output);

    cJSON_AddItemToObject(session, "audio", audio);
    cJSON_AddStringToObject(session, "model", DOUBAO_MODEL);
    cJSON_AddItemToObject(root, "session", session);

    ESP_LOGI(TAG, "Send session.create: voice=%s", voice);
    esp_err_t err = ws_send_json(root);
    cJSON_Delete(root);
    return err;
}

static esp_err_t send_audio_append(const uint8_t *audio, uint32_t len)
{
    if (audio == NULL || len == 0) {
        return ESP_OK;
    }
    /* base64 编码音频 */
    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, audio, len);
    char *b64 = (char *)malloc(b64_len + 1);
    if (b64 == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t olen = 0;
    mbedtls_base64_encode((unsigned char *)b64, b64_len + 1, &olen, audio, len);
    b64[olen] = '\0';

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        free(b64);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "input_audio_buffer.append");
    cJSON_AddStringToObject(root, "audio", b64);
    free(b64);

    esp_err_t err = ws_send_json(root);
    cJSON_Delete(root);
    return err;
}

esp_err_t openai_send_audio_commit(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "input_audio_buffer.commit");
    esp_err_t err = ws_send_json(root);
    cJSON_Delete(root);
    if (err == ESP_OK) {
        /* LOCAL FIX (2026-11-03): 记录 commit 等待服务端 committed 确认 */
        s_ctx.commit_pending = 1;
        s_ctx.last_commit_ms = (uint32_t)(esp_timer_get_time() / 1000);
    }
    return err;
}

esp_err_t openai_send_response_create(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "response.create");
    esp_err_t err = ws_send_json(root);
    cJSON_Delete(root);
    return err;
}

esp_err_t openai_send_audio_mute(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "input_audio_mute.commit");
    esp_err_t err = ws_send_json(root);
    cJSON_Delete(root);
    return err;
}

esp_err_t openai_send_audio_unmute(void)
{
    /* LOCAL FIX (2026-11-02): PTT 按下即恢复输入——auto-stop 置位的 auto_input_stopped
     * 若不复位，后续按键的 append 全部被 openai_send_audio 门控拦截（服务端收不到音频）。 */
    s_ctx.auto_input_stopped = false;
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "input_audio_unmute.commit");
    esp_err_t err = ws_send_json(root);
    cJSON_Delete(root);
    return err;
}

static esp_err_t send_session_close(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "session.close");
    esp_err_t err = ws_send_json(root);
    cJSON_Delete(root);
    return err;
}

/* ---------------- 接收解析 ---------------- */

static void handle_server_event(cJSON *root)
{
    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (type_item == NULL || !cJSON_IsString(type_item)) {
        return;
    }
    const char *type = type_item->valuestring;

    /* LOCAL FIX (2026-11-02): 服务端一旦开始响应（response.* 事件），立即自动
     * commit+mute 停止上送输入。manual32 实证：output_audio.started 后客户端仍
     * 持续 input_audio_buffer.append，服务端随即断开连接（transport_poll_write
     * 错误 → esp_tls_conn_read errno=11 → WS error），audio.delta 永远收不到。
     * 自动停输后服务端可完整下发 TTS。 */
    if (strncmp(type, "response.", 9) == 0 && !s_ctx.auto_input_stopped) {
        s_ctx.auto_input_stopped = true;
        ESP_LOGI(TAG, "Server responding (%s), auto stop input (commit+mute)", type);
        openai_send_audio_commit();
        openai_send_audio_mute();
    }

    if (strcmp(type, "input_audio_buffer.committed") == 0) {
        s_ctx.commit_pending = 0;   /* LOCAL FIX (2026-11-03): commit 已确认 */
    } else if (strcmp(type, EV_SESSION_CREATED) == 0) {
        ESP_LOGI(TAG, "session.created");
        s_ctx.session_started = true;
        s_ctx.auto_input_stopped = false;
        if (s_ctx.cfg.audio_event_handler != NULL) {
            s_ctx.cfg.audio_event_handler(ESP_PEER_STATE_DATA_CHANNEL_CONNECTED, NULL, s_ctx.cfg.ctx);
        }
    } else if (strcmp(type, EV_SESSION_CLOSED) == 0) {
        ESP_LOGI(TAG, "session.closed");
        s_ctx.session_started = false;
        /* LOCAL FIX (2026-11-03): 服务端空闲超时主动关会话。不手动 stop+start
         * （manual54 实证手动 stop 触发崩溃）——服务端随后关闭连接，
         * esp_websocket_client 自动重连并重发 session.create（manual54 91796 实证）。 */
        s_ctx.commit_pending = 0;
    } else if (strcmp(type, EV_OUT_AUDIO_STARTED) == 0) {
        ESP_LOGD(TAG, "output_audio.started");
    } else if (strcmp(type, EV_OUT_AUDIO_DELTA) == 0) {
        /* LOCAL FIX (2026-10-30): 豆包 duplex 音频在 delta 字段（官方文档明确），
         * 兼容 audio 字段（OpenAI 风格），两者都尝试读取。 */
        cJSON *audio_item = cJSON_GetObjectItem(root, "delta");
        if (audio_item == NULL || !cJSON_IsString(audio_item)) {
            audio_item = cJSON_GetObjectItem(root, "audio");
        }
        if (audio_item != NULL && cJSON_IsString(audio_item) && s_ctx.cfg.audio_data_handler != NULL) {
            const char *b64 = audio_item->valuestring;
            size_t b64_len = strlen(b64);
            /* v20b (2026-11-03): 单次解码+PSRAM 延迟分配——static 48KB BSS 占内部 RAM
             * 导致 WiFi init ESP_ERR_NO_MEM，改为首次 delta 时从 PSRAM malloc 并复用。
             * 计算解码上限 b64_len*3/4+3，直接单次 decode（省去原两次调用）。 */
            size_t dec_cap = (b64_len * 3) / 4 + 3;
            static uint8_t *s_dec_buf = NULL;
            static size_t s_dec_cap = 0;
            if (s_dec_buf == NULL || dec_cap > s_dec_cap) {
                if (s_dec_buf != NULL) {
                    free(s_dec_buf);
                    s_dec_buf = NULL;
                }
                s_dec_buf = (uint8_t *)heap_caps_malloc(dec_cap, MALLOC_CAP_SPIRAM);
                if (s_dec_buf != NULL) {
                    s_dec_cap = dec_cap;
                    ESP_LOGI(TAG, "base64 decode PSRAM buffer: %d bytes", (int)dec_cap);
                }
            }
            uint8_t *dec = (s_dec_buf != NULL && dec_cap <= s_dec_cap) ? s_dec_buf
                         : (dec_cap <= 4096) ? (uint8_t *)malloc(dec_cap) : NULL;
            if (dec != NULL) {
                size_t olen = 0;
                int rc = mbedtls_base64_decode(dec, dec_cap, &olen,
                                               (const unsigned char *)b64, b64_len);
                if (rc == 0 && olen > 0) {
                    /* LOCAL FIX (2026-11-01): 播放诊断——delta 节流日志 */
                    static int s_delta_count = 0;
                    if ((++s_delta_count % 50) == 1) {
                        ESP_LOGI(TAG, "audio delta: %d bytes, total pkts=%d", (int)olen, s_delta_count);
                    }
                    s_ctx.cfg.audio_data_handler(dec, (int)olen, s_ctx.cfg.ctx);
                }
                if (dec != s_dec_buf) {
                    free(dec);
                }
            }
        }
    } else if (strcmp(type, EV_OUT_AUDIO_DONE) == 0) {
        ESP_LOGI(TAG, "output_audio.done");
    } else if (strcmp(type, EV_ERR) == 0) {
        const char *msg = "";
        cJSON *m = cJSON_GetObjectItem(root, "message");
        if (m != NULL && cJSON_IsString(m)) {
            msg = m->valuestring;
        }
        ESP_LOGE(TAG, "Server error: %s", msg);
        /* LOCAL FIX (2026-11-03): 会话失效错误——不手动 stop+start（manual54 实证
         * 手动 stop 触发 Interrupt wdt timeout / LoadProhibited 崩溃）。
         * 服务端随后关闭连接 -> esp_websocket_client 自动重连 -> session.create（安全，
         * manual54 91796 实证）。45000000=no session active；55000000=空闲超时关会话。 */
        if (strstr(msg, "45000000") != NULL || strstr(msg, "55000000") != NULL) {
            ESP_LOGW(TAG, "session expired (%s), wait auto reconnect", strstr(msg, "45000000") != NULL ? "45000000" : "55000000");
            s_ctx.session_started = false;
            s_ctx.commit_pending = 0;
        }
    } else {
        ESP_LOGD(TAG, "Unhandled event: %s", type);
    }
}

/* ---------------- WebSocket 回调 ---------------- */

static void doubao_event_handler(void *handler_args, esp_event_base_t base,
                                 int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS connected");
        s_ctx.ws_connected = true;
        s_ctx.session_started = false;
        s_ctx.ever_connected = true;   /* LOCAL FIX (2026-10-29) */
        send_session_create();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WS disconnected");
        s_ctx.ws_connected = false;
        s_ctx.session_started = false;
        /* LOCAL FIX (2026-11-02): 运行中断链（ever_connected=true，曾握手成功）不通知上层
         * AgentMgr Stop——WS 按 reconnect_timeout_ms=5000 自动重连，CONNECTED 事件重发
         * session.create 恢复会话，上层保持运行，按键可继续说话。
         * 仅握手失败（401 等，从未成功）才通知断开。 */
        if (!s_ctx.ever_connected && s_ctx.cfg.audio_event_handler != NULL) {
            s_ctx.cfg.audio_event_handler(ESP_PEER_STATE_DISCONNECTED, NULL, s_ctx.cfg.ctx);
        }
        s_ctx.ever_connected = false;
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error");
        if (!s_ctx.ever_connected) {
            /* LOCAL FIX (2026-10-29): handshake failure (401 etc) - tear the client down
             * instead of letting esp_websocket_client reconnect-loop, which repeatedly
             * rebuilds transports and corrupts the transport list (heap assert on destroy).
             * destroy() called from the ws task context defers to task-exit self-destroy,
             * so the task exits cleanly; openai_deinit() then sees ws==NULL and skips. */
            if (s_ctx.ws != NULL) {
                esp_websocket_client_destroy(s_ctx.ws);
                s_ctx.ws = NULL;
                s_ctx.ws_connected = false;
                s_ctx.session_started = false;
                s_ctx.ever_connected = false;
            }
        } else {
            /* LOCAL FIX (2026-11-02): 运行中断链（网络瞬断/服务端 RST）不得 destroy——
             * 保留 client 让 esp_websocket_client 按 reconnect_timeout_ms=5000 自动重连，
             * 重连后 CONNECTED 事件重发 session.create，会话恢复，按键可继续说话。 */
            s_ctx.ws_connected = false;
            s_ctx.session_started = false;
        }
        break;
    default:
        break;
    }
}

static void doubao_data_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_id;

    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (data == NULL) {
        return;
    }
    if (data->data_len <= 0) {
        return;
    }
    if (data->op_code != 0x01) {         /* 只处理文本帧 */
        return;
    }
    /* LOCAL FIX (2026-10-30): WS 分片重组——音频 delta 帧可超过
     * CONFIG_WS_BUFFER_SIZE(4096)，esp_websocket_client 分片回调
     * （每片 data_ptr 指向被覆盖的同一 rx_buffer，只含本片内容），
     * 单片 JSON 不完整导致 cJSON 解析失败、音频被静默丢弃。
     * 按 payload_offset 累积到 PSRAM 静态缓冲（EXT_RAM_BSS_ATTR，
     * 不占 internal DRAM——此前 16KB internal 缓冲导致 WiFi 初始化
     * 内存不足 ESP_ERR_NO_MEM），收齐整条消息再解析。 */
    static EXT_RAM_BSS_ATTR char s_ws_accum[65536];
    static EXT_RAM_BSS_ATTR int  s_ws_accum_len = 0;
    static EXT_RAM_BSS_ATTR bool s_ws_dropping = false;
    if (data->payload_offset == 0) {
        s_ws_accum_len = 0;              /* 新消息开始 */
        s_ws_dropping = false;
    }
    if (s_ws_dropping) {
        /* LOCAL FIX (2026-10-31): 溢出后整条消息作废——跳过剩余分片，
         * 避免把后半段分片拼成残缺 JSON（此前会静默解析失败丢音频）。 */
        if (data->payload_offset + data->data_len >= data->payload_len) {
            s_ws_dropping = false;       /* 本条超大消息结束，恢复正常 */
        }
        return;
    }
    if (s_ws_accum_len + data->data_len > (int)sizeof(s_ws_accum)) {
        ESP_LOGW(TAG, "WS accum overflow, dropping (len=%d)", data->data_len);
        s_ws_accum_len = 0;
        s_ws_dropping = true;            /* 进入丢弃模式，跳过本条剩余分片 */
        return;
    }
    memcpy(s_ws_accum + s_ws_accum_len, data->data_ptr, data->data_len);
    s_ws_accum_len += data->data_len;
    if (data->payload_offset + data->data_len < data->payload_len) {
        return;                          /* 分片未完，等下一片 */
    }
    /* 整条消息收齐：解析累积缓冲 */
    ESP_LOGI(TAG, "WS RX op=0x%02x len=%d payload=[%.*s]", data->op_code, s_ws_accum_len,
             (s_ws_accum_len > 300 ? 300 : s_ws_accum_len), s_ws_accum);
    cJSON *root = cJSON_ParseWithLength(s_ws_accum, s_ws_accum_len);
    s_ws_accum_len = 0;
    if (root == NULL) {
        ESP_LOGD(TAG, "JSON parse failed: %.*s", data->data_len, data->data_ptr);
        return;
    }
    /* LOCAL FIX (2026-10-30): 豆包 duplex 服务端所有下行事件都是 JSON 数组
     * [{...}]（实测 payload 以 [ 开头），必须拆出首元素再按对象解析，
     * 否则 cJSON_GetObjectItem 只会拿到数组元素对象本身，delta/audio 字段
     * 永远取不到（output_audio.delta 音频被静默丢弃）。 */
    cJSON *evt = root;
    if (cJSON_IsArray(root) && cJSON_GetArraySize(root) > 0) {
        evt = cJSON_GetArrayItem(root, 0);
    }
    handle_server_event(evt);
    cJSON_Delete(root);
}

/* ---------------- 对外接口 ---------------- */

esp_err_t openai_init(openai_config_t *config)
{
    if (config == NULL || config->api_key == NULL || config->api_key[0] == '\0') {
        ESP_LOGE(TAG, "Invalid config: api_key (X-Api-Key) is required");
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.cfg = *config;
    s_ctx.cfg.model = strdup(config->model ? config->model : "");
    s_ctx.cfg.api_key = strdup(config->api_key);
    s_ctx.cfg.voice = strdup(config->voice ? config->voice : "");
    s_ctx.cfg.app_id = strdup(config->app_id ? config->app_id : "");
    if (s_ctx.cfg.model == NULL || s_ctx.cfg.api_key == NULL ||
        s_ctx.cfg.voice == NULL || s_ctx.cfg.app_id == NULL) {
        free((void *)s_ctx.cfg.model);
        free((void *)s_ctx.cfg.api_key);
        free((void *)s_ctx.cfg.voice);
        free((void *)s_ctx.cfg.app_id);
        memset(&s_ctx, 0, sizeof(s_ctx));
        return ESP_ERR_NO_MEM;
    }
    s_ctx.lock = xSemaphoreCreateMutex();
    if (s_ctx.lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_ctx.initialized = true;

    /* LOCAL FIX (2026-11-03): 启动 commit 确认看门狗任务 */
    if (xTaskCreate(commit_watchdog_task, "commit_wd", 3072, NULL, 5, &s_ctx.commit_wd) != pdPASS) {
        s_ctx.commit_wd = NULL;
    }

    ESP_LOGI(TAG, "init ok: endpoint=%s", DOUBAO_WS_URI);
    return ESP_OK;
}

esp_err_t openai_deinit(void)
{
    if (!s_ctx.initialized) {
        return ESP_OK;
    }
    if (s_ctx.ws != NULL) {
        if (s_ctx.ws_connected && s_ctx.session_started) {
            send_session_close();
        }
        esp_websocket_client_stop(s_ctx.ws);
        esp_websocket_client_destroy(s_ctx.ws);
        s_ctx.ws = NULL;
    }
    free((void *)s_ctx.cfg.model);
    free((void *)s_ctx.cfg.api_key);
    free((void *)s_ctx.cfg.voice);
    free((void *)s_ctx.cfg.app_id);
    if (s_ctx.commit_wd != NULL) {
        vTaskDelete(s_ctx.commit_wd);
        s_ctx.commit_wd = NULL;
    }
    if (s_ctx.lock != NULL) {
        vSemaphoreDelete(s_ctx.lock);
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
    ESP_LOGI(TAG, "deinit done");
    return ESP_OK;
}

esp_err_t openai_start(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_ctx.agent_started = true;

    ESP_LOGI(TAG, "Heap before WS start: internal=%d 8bit=%d psram=%d largest=%d",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_8BIT),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    if (s_ctx.ws == NULL) {
        esp_websocket_client_config_t ws_cfg = {0};
        ws_cfg.uri = DOUBAO_WS_URI;
        ws_cfg.network_timeout_ms = 90000; /* LOCAL FIX (2026-11-02): 10s->90s——服务端 TTS 长文本合成期间
                                            * 无数据，10s 超时导致 transport_ws EAGAIN 误判断连，audio.delta 收不到 */
        ws_cfg.buffer_size = 40 * 1024;   /* v20 (2026-11-03): 8KB->40KB——豆包 delta 帧含 22-36KB base64 文本，
                                      * 8KB 缓冲导致大帧分片接收/"WS frame exceeds buffer"断连，音频块延迟→卡顿。
                                      * 40KB 覆盖最大帧(36KB b64+JSON开销)且 PSRAM 充足可承受。 */
        ws_cfg.reconnect_timeout_ms = 5000;
        /* LOCAL FIX (2026-11-03): pingpong 超时 120s(默认)->45s——服务端 TTS 长文本合成期间
         * 无法回复 PING，15s 实测过短导致误判断连("no PONG received for more than 15 seconds")；
         * 45s 兼顾假死检测(>45s 无响应必断)和 TTS 合成静默期(实测可达 20-30s)。 */
        ws_cfg.pingpong_timeout_sec = 45;
        ws_cfg.task_stack = 4096;   /* LOCAL FIX (2026-10-29): 栈已由 esp_websocket_client 改放 PSRAM，恢复到安全大小 */
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        ws_cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif
        static char headers_buf[512];
        ESP_LOGI(TAG, "AUTH key len=%d prefix=[%.6s] model=[%s]", (int)strlen(s_ctx.cfg.api_key), s_ctx.cfg.api_key, s_ctx.cfg.model);
        snprintf(headers_buf, sizeof(headers_buf),
                 "X-Api-Key: %s\r\n"
                 "X-Api-Resource-Id: volc.speech.dialog\r\n",
                 s_ctx.cfg.api_key);
        ws_cfg.headers = headers_buf;

        ESP_LOGI(TAG, "Connecting to %s ...", DOUBAO_WS_URI);
        s_ctx.ws = esp_websocket_client_init(&ws_cfg);
        if (s_ctx.ws == NULL) {
            ESP_LOGE(TAG, "Failed to init websocket client");
            return ESP_FAIL;
        }
        esp_websocket_register_events(s_ctx.ws, WEBSOCKET_EVENT_ANY, doubao_event_handler, NULL);
        esp_websocket_register_events(s_ctx.ws, WEBSOCKET_EVENT_DATA, doubao_data_handler, NULL);
        esp_err_t err = esp_websocket_client_start(s_ctx.ws);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start websocket client: %s", esp_err_to_name(err));
            return err;
        }
    } else if (s_ctx.ws != NULL && !s_ctx.ws_connected) {
        /* LOCAL FIX (2026-11-03): esp_websocket_client 假死盲区——服务端第 1 轮推完
         * TTS 后不发 done、静默关连接，本地不触发 DISCONNECTED/ERROR，ws_connected 仍 true；
         * 一旦感知断开(ws_connected=false)，PTT 按下时主动 stop+start 重建（普通任务上下文
         * 安全，ws_send_json 148 行实证），CONNECTED 事件自动重发 session.create，恢复会话。 */
        ESP_LOGW(TAG, "ws down on PTT, reconnect");
        esp_websocket_client_stop(s_ctx.ws);
        esp_websocket_client_start(s_ctx.ws);
    } else if (s_ctx.ws_connected && !s_ctx.session_started) {
        /* 连接已就绪但会话未建立（如断线重连后） */
        send_session_create();
    } else if (s_ctx.ws_connected && s_ctx.session_started) {
        /* 会话复用：恢复音频流（解除静音保活） */
        openai_send_audio_unmute();
    }
    return ESP_OK;
}

esp_err_t openai_stop(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_ctx.agent_started = false;
    /* PTT 松开：提交音频结束 + 静音保活，保持连接与会话复用 */
    if (s_ctx.ws != NULL && s_ctx.ws_connected && s_ctx.session_started) {
        openai_send_audio_commit();
        openai_send_audio_mute();
    }
    return ESP_OK;
}

void openai_send_audio(uint8_t *data, size_t size)
{
    if (!s_ctx.initialized || !s_ctx.agent_started || !s_ctx.session_started) {
        return;
    }
    if (data == NULL || size == 0) {
        return;
    }
    /* LOCAL FIX (2026-11-02): 服务端已开始响应后不再上送输入音频。
     * manual33 实证：auto stop 只发 mute.commit 但 openai_send_audio 仍持续
     * append，服务端 muted 后仍收到音频，output_audio.started 后 ~3s 连接断开，
     * audio.delta 永远收不到。此处彻底门控，让服务端安静推完 TTS。 */
    if (s_ctx.auto_input_stopped) {
        return;
    }
    send_audio_append(data, (uint32_t)size);

    /* MIC LEVEL: accumulate opus frame bytes, print avg every ~1s (16k rate, 20ms frames = 50/s) */
    static uint32_t s_mic_acc = 0;
    static uint32_t s_mic_frames = 0;
    static uint32_t s_mic_last_ms = 0;
    s_mic_acc += (uint32_t)size;
    s_mic_frames++;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_mic_last_ms == 0) {
        s_mic_last_ms = now;
    }
    if (now - s_mic_last_ms >= 1000) {
        uint32_t avg = (s_mic_frames > 0) ? (s_mic_acc / s_mic_frames) : 0;
        ESP_LOGI("mic_level", "MIC: %lu frames, avg frame %lu bytes, %lu bytes/s (silence<=8, speech~100-250)", (unsigned long)s_mic_frames, (unsigned long)avg, (unsigned long)s_mic_acc);
        s_mic_acc = 0;
        s_mic_frames = 0;
        s_mic_last_ms = now;
    }
}

esp_err_t openai_send_text(const char *text)
{
    if (!s_ctx.initialized || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "speech_text_buffer.replacement.append");
    cJSON_AddStringToObject(root, "text", text);
    esp_err_t err = ws_send_json(root);
    cJSON_Delete(root);
    return err;
}
