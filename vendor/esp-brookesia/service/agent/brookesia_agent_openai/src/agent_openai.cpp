/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <expected>
#include <functional>
#include <utility>
#include <algorithm>
#include <cstring>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "esp_peer.h"
#include "brookesia/agent_openai/macro_configs.h"
#if !BROOKESIA_AGENT_OPENAI_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/utils.hpp"
#include "brookesia/lib_utils/function_guard.hpp"
#include "brookesia/lib_utils/plugin.hpp"
#include "brookesia/agent_manager/manager.hpp"
#include "brookesia/agent_openai/agent_openai.hpp"
#include "openai.h"
#include "openai_datachannel.hpp"

namespace esp_brookesia::agent {

std::string Openai::get_component_version()
{
    return make_version(
               BROOKESIA_AGENT_OPENAI_VER_MAJOR, BROOKESIA_AGENT_OPENAI_VER_MINOR, BROOKESIA_AGENT_OPENAI_VER_PATCH
           );
}

bool Openai::on_init()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_LOGI(
        "Version: %1%.%2%.%3%", BROOKESIA_AGENT_OPENAI_VER_MAJOR, BROOKESIA_AGENT_OPENAI_VER_MINOR,
        BROOKESIA_AGENT_OPENAI_VER_PATCH
    );

    return true;
}

bool Openai::on_activate()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    auto &info = data_info_;
    BROOKESIA_LOGD("Start with info: %1%", BROOKESIA_DESCRIBE_TO_STR(info));

    BROOKESIA_CHECK_FALSE_RETURN(validate_info(info), false, "Invalid info");

    openai_config_t config = {
        .audio_data_handler = +[](uint8_t *data, int len, void *ctx)
        {
            auto self = static_cast<Openai *>(ctx);
            BROOKESIA_CHECK_NULL_EXIT(self, "Invalid context");
            BROOKESIA_CHECK_FALSE_EXIT(self->on_audio_data(data, len), "Failed to on audio data");
        },
        .audio_event_handler = +[](int event, uint8_t *data, void *ctx)
        {
            auto self = static_cast<Openai *>(ctx);
            BROOKESIA_CHECK_NULL_EXIT(self, "Invalid context");
            BROOKESIA_CHECK_FALSE_EXIT(self->on_audio_event(event, data), "Failed to on audio event");
        },
        .model = info.model.empty() ? OPENAI_DEFAULT_MODEL : info.model.c_str(),
        .api_key = info.api_key.c_str(),
        .voice = info.voice.empty() ? OPENAI_DEFAULT_VOICE : info.voice.c_str(),
        .app_id = info.app_id.c_str(),
        .connect_timeout_ms = OPENAI_DEFAULT_CONNECT_TIMEOUT_MS,
        .ctx = this,
    };
    BROOKESIA_CHECK_ESP_ERR_RETURN(openai_init(&config), false, "Failed to init openai");
    is_openai_initialized_ = true;

    trigger_general_event(GeneralEvent::Activated);

    return true;
}

bool Openai::on_startup()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_CHECK_ESP_ERR_RETURN(openai_start(), false, "Failed to start openai");
    is_openai_started_ = true;

    return true;
}

void Openai::on_shutdown()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (is_openai_started()) {
        BROOKESIA_CHECK_ESP_ERR_EXECUTE(openai_stop(), {}, {
            BROOKESIA_LOGE("Failed to stop openai");
        });
        is_openai_started_ = false;
    }
    if (is_openai_initialized()) {
        BROOKESIA_CHECK_ESP_ERR_EXECUTE(openai_deinit(), {}, {
            BROOKESIA_LOGE("Failed to deinit openai");
        });
        is_openai_initialized_ = false;
    }
}

bool Openai::on_sleep()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    trigger_general_event(GeneralEvent::Slept);

    return true;
}

bool Openai::on_wakeup()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    trigger_general_event(GeneralEvent::Awake);

    return true;
}

bool Openai::on_encoder_data_ready(const uint8_t *data, size_t data_size)
{
    // BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    // BROOKESIA_LOGD("Params: data(%1%), data_size(%2%)", data, data_size);

    if (!is_openai_started()) {
        // BROOKESIA_LOGD("Openai is not started, skip");
        return true;
    }

    openai_send_audio(const_cast<uint8_t *>(data), data_size);

    return true;
}

bool Openai::on_manual_start_listening()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    /* LOCAL FIX (2026-10-30): PTT 按下恢复麦克风，解除静音保活，
     * 否则服务器仍处于 mute 状态不接收新音频。 */
    if (!is_openai_started()) {
        BROOKESIA_LOGW("Openai is not started, skip unmute");
        return true;
    }

    esp_err_t err = openai_send_audio_unmute();
    if (err != ESP_OK) {
        BROOKESIA_LOGE("Failed to send audio unmute: %s", esp_err_to_name(err));
    } else {
        BROOKESIA_LOGI("Sent input_audio_unmute.commit (PTT pressed)");
    }

    return true;
}

bool Openai::on_manual_stop_listening()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    /* LOCAL FIX (2026-10-30): PTT 松开必须发 input_audio_buffer.commit（=豆包 duplex 的 EndASR），
     * 否则服务器永远等不到音频输入结束，60s 后 AudioServerNoAudioInputTooLongError
     * 关会话、不生成回复（喇叭无声的直接根因）。 */

    if (!is_openai_started()) {
        BROOKESIA_LOGW("Openai is not started, skip commit");
        return true;
    }

    esp_err_t err = openai_send_audio_commit();
    if (err != ESP_OK) {
        BROOKESIA_LOGE("Failed to send audio commit: %s", esp_err_to_name(err));
    } else {
        BROOKESIA_LOGI("Sent input_audio_buffer.commit (PTT released)");
    }

    /* LOCAL FIX (2026-10-30): 豆包 duplex 协议无 response.create（服务器 45000000 unknown event 实锤）。
     * commit(EndASR) 后必须发 input_audio_mute.commit 静音保活（官方文档：
     * "模型依赖上行音频流保活，关闭麦克风后须主动发送静音事件，否则服务端因持续收不到
     * 音频输入导致模型无响应"）。openai_stop() 不会在此路径执行，mute 必须在此发出。 */
    err = openai_send_audio_mute();
    if (err != ESP_OK) {
        BROOKESIA_LOGE("Failed to send audio mute: %s", esp_err_to_name(err));
    } else {
        BROOKESIA_LOGI("Sent input_audio_mute.commit (PTT released)");
    }

    return true;
}

bool Openai::set_info(const boost::json::object &info)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_LOGD("Params: info(%1%)", BROOKESIA_DESCRIBE_TO_STR(info));

    OpenaiInfo openai_info;

    auto success = BROOKESIA_DESCRIBE_FROM_JSON(info, openai_info);
    BROOKESIA_CHECK_FALSE_RETURN(
        success, false, "Failed to deserialize openai info: %1%", BROOKESIA_DESCRIBE_TO_STR(info)
    );

    auto current_info_str = BROOKESIA_DESCRIBE_JSON_SERIALIZE(data_info_);
    auto new_info_str = BROOKESIA_DESCRIBE_JSON_SERIALIZE(openai_info);
    if (current_info_str == new_info_str) {
        BROOKESIA_LOGI("Info is the same, skip setting");
        return true;
    }

    BROOKESIA_CHECK_FALSE_RETURN(validate_info(openai_info), false, "Invalid info");

    data_info_ = std::move(openai_info);

    return true;
}

bool Openai::reset_data()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    data_info_ = OpenaiInfo{};

    BROOKESIA_LOGI("Reset all data");

    return true;
}

bool Openai::validate_info(OpenaiInfo &info)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_CHECK_FALSE_RETURN(!info.model.empty(), false, "Model is empty");
    BROOKESIA_CHECK_FALSE_RETURN(!info.api_key.empty(), false, "API key is empty");

    return true;
}

bool Openai::on_audio_data(uint8_t *data, int len)
{
    // BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    // BROOKESIA_LOGD("Params: data(%1%), len(%2%)", data, len);

    if (!is_openai_started()) {
        // BROOKESIA_LOGD("Openai is not started, skip");
        return true;
    }

    /* LOCAL FIX (2026-10-31): 豆包 Realtime 下行 PCM s16le 24k，
     * decoder 管线为 16k，先 24k->16k 重采样再喂解码器。
     * v12：跨帧连续重采样——全局相位 s_stream_pos(24k 流位置*256)跨帧推进，
     * 每帧输出位置 = 帧起始相位 + i*1.5，帧内索引 = 输出位置 - 帧起始(相对本帧
     * buffer)，两者结合既避免旧实现"每帧从 0 重采样"的帧边界 1.5 采样回跳毛刺，
     * 也避免 v11 用全局位置直接索引本地 buffer 导致的错位/越界(静音+噪音)。
     * 采样用字节读取，避免 WS 缓冲区未对齐时 Xtensa L16I 异常。 */
    size_t in_samples = (size_t)len / 2;
    if (in_samples == 0) {
        return true;
    }
    size_t out_cap = (in_samples * 2) / 3;
    size_t out_bytes = out_cap * sizeof(int16_t);
    /* v22 (2026-11-03): 重采样缓冲改用静态 PSRAM 延迟分配——此前每次 delta 回调都
     * heap_caps_malloc+heap_caps_free 15-24KB，频繁分配导致 PSRAM 堆碎片化+分配延迟。
     * 改为首次分配后复用（仅扩容不缩容），与 openai.c base64 解码缓冲同策略。 */
    static int16_t *s_rs_buf = nullptr;
    static size_t s_rs_cap = 0;
    if (s_rs_buf == nullptr || out_bytes > s_rs_cap) {
        if (s_rs_buf != nullptr) {
            heap_caps_free(s_rs_buf);
            s_rs_buf = nullptr;
        }
        s_rs_buf = (int16_t *)heap_caps_malloc(out_bytes, MALLOC_CAP_SPIRAM);
        if (s_rs_buf != nullptr) {
            s_rs_cap = out_bytes;
            BROOKESIA_LOGI("resample PSRAM buffer: %d bytes", (int)out_bytes);
        }
    }
    int16_t *pcm_16k = (s_rs_buf != nullptr && out_bytes <= s_rs_cap) ? s_rs_buf
                       : (out_bytes <= 4096) ? (int16_t *)malloc(out_bytes) : nullptr;
    if (pcm_16k == nullptr) {
        BROOKESIA_LOGE("Failed to alloc resample buffer");
        return true;
    }
    static uint32_t s_stream_pos = 0;      /* 全局 24k 流位置 *256，跨帧连续 */
    static int16_t s_tail[2] = {0, 0};     /* 上一帧末尾 2 采样（越界平滑用） */
    static bool s_first = true;
    if (s_first) {
        s_stream_pos = 0;
        s_first = false;
    }
    const uint8_t *pb = data;
    const uint32_t frame_start = s_stream_pos; /* 本帧起始全局相位 */
    for (size_t i = 0; i < out_cap; ++i) {
        uint32_t gpos = frame_start + (uint32_t)(i * 384); /* 全局流位置 */
        uint32_t lpos = gpos - s_stream_pos;               /* 帧内相对位置 */
        size_t ipos = lpos >> 8;
        uint32_t frac = lpos & 0xFF;
        int16_t s0, s1;
        if (ipos < in_samples) {
            s0 = (int16_t)((uint16_t)pb[ipos * 2] | ((uint16_t)pb[ipos * 2 + 1] << 8));
            if (ipos + 1 < in_samples) {
                s1 = (int16_t)((uint16_t)pb[(ipos + 1) * 2] | ((uint16_t)pb[(ipos + 1) * 2 + 1] << 8));
            } else {
                s1 = s0;
            }
        } else {
            /* 帧尾越界(相位落点逼近帧末)：用本帧末尾采样保持平滑 */
            s0 = s1 = (int16_t)((uint16_t)pb[(in_samples - 1) * 2] | ((uint16_t)pb[(in_samples - 1) * 2 + 1] << 8));
        }
        int32_t out = (int32_t)s0 + (((int32_t)(s1 - s0) * (int32_t)frac) >> 8);
        pcm_16k[i] = (int16_t)out;
    }
    s_stream_pos = frame_start + (uint32_t)(out_cap * 384); /* 帧末相位=下一帧起始 */
    s_tail[0] = (in_samples >= 2)
                    ? (int16_t)((uint16_t)pb[(in_samples - 2) * 2] | ((uint16_t)pb[(in_samples - 2) * 2 + 1] << 8))
                    : s_tail[1];
    s_tail[1] = (int16_t)((uint16_t)pb[(in_samples - 1) * 2] | ((uint16_t)pb[(in_samples - 1) * 2 + 1] << 8));
    /* LOCAL FIX (2026-10-31): 预卷方案最终回退——v1(0.5s)加剧超时丢帧、v2(0.75s)
     * 短回复(<0.75s)永不喂出+块间停顿>1.5s丢弃缓冲导致无声。同步写模型下逐帧直喂
     * 才是正确基线；卡顿主要由服务器 TTS 块间生成停顿(0.3-0.75s)造成，
     * 固件侧已通过放宽 AGENT_AUDIO_WRITE_COMPLETION_TIMEOUT_MS=1500ms 消除丢帧。
     * LOCAL FIX (2026-10-31): TTS 帧拆分为小片连续喂入——大块(~13.7KB)直接入队会让
     * decoder/HAL 消费粒度变粗、播放边界不平滑；拆成 4KB 小片后每片约 0.125s，
     * I2S 缓冲始终有数据，配合 decoder 侧预卷吸收块间生成停顿。 */
    const uint8_t *feed_p = (const uint8_t *)pcm_16k;
    size_t feed_remain = out_cap * sizeof(int16_t);
    constexpr size_t FEED_CHUNK_SIZE = 4096;
    bool ok = true;
    while (feed_remain > 0) {
        size_t n = (feed_remain > FEED_CHUNK_SIZE) ? FEED_CHUNK_SIZE : feed_remain;
        if (!feed_audio_decoder_data(feed_p, n)) {
            /* LOCAL FIX (2026-11-01): 播放诊断——打印 feed 失败位置 */
            BROOKESIA_LOGE("Failed to feed audio decoder: remain=%1%, chunk=%2%, in=%3%", (int)feed_remain, (int)n, (int)in_samples);
            ok = false;
            break;
        }
        feed_p += n;
        feed_remain -= n;
    }
    /* v22: 静态 PSRAM 缓冲复用，不再 free */
    if (pcm_16k != s_rs_buf && pcm_16k != nullptr) {
        free(pcm_16k);
    }
    /* LOCAL FIX (2026-10-30): 播放诊断——首次解码成功时打印一次 */
    static bool s_audio_played = false;
    if (!s_audio_played) {
        BROOKESIA_LOGI("AUDIO: first resample feed ok=%d in=%d out=%d", ok, (int)in_samples, (int)out_cap);
        s_audio_played = true;
    }
    BROOKESIA_CHECK_FALSE_RETURN(ok, false, "Failed to feed audio data");

    return true;
}

bool Openai::on_audio_event(int event, uint8_t *data)
{
    // BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    // BROOKESIA_LOGD("Params: event(%1%), data(%2%)", event, data);

    std::function<void(void)> task_func;

    esp_peer_state_t state = static_cast<esp_peer_state_t>(event);
    switch (state) {
    case ESP_PEER_STATE_CONNECTED:
        BROOKESIA_LOGI("peer connected");
        break;
    case ESP_PEER_STATE_DISCONNECTED:
        BROOKESIA_LOGE( "peer disconnected");
        // Set task function to stop agent
        task_func = [this]() {
            BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
            trigger_general_event(GeneralEvent::Stopped);
        };
        break;
    case ESP_PEER_STATE_DATA_CHANNEL_CONNECTED:
        BROOKESIA_LOGI("peer data channel connected");
        // Set task function to start agent
        task_func = [this]() {
            BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
            trigger_general_event(GeneralEvent::Started);
        };
        break;
    default:
        // int evt = (int)event;
        // if (evt == ESP_PEER_MSG_EVENT) {
        //     cJSON *json = cJSON_Parse((char *)data);
        //     if (json) {
        //         openai_datachannel_handle_message(json, (char *)data);
        //         cJSON_Delete(json);
        //     } else {
        //         BROOKESIA_LOGE("cJSON parse failed: %s", (char *)data);
        //     }
        // }
        break;
    }

    if (task_func) {
        auto group = get_state_task_group();
        auto scheduler = get_task_scheduler();
        BROOKESIA_CHECK_NULL_RETURN(scheduler, false, "Scheduler is not available");
        auto result = scheduler->post(std::move(task_func), nullptr, group);
        BROOKESIA_CHECK_FALSE_RETURN(result, false, "Failed to post task function");
    }

    return true;
}

#if BROOKESIA_AGENT_OPENAI_ENABLE_AUTO_REGISTER
BROOKESIA_PLUGIN_REGISTER_SINGLETON(
    Base, Openai, Openai::get_instance().get_attributes().get_name(), Openai::get_instance()
);
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    service::ServiceBase, Openai, Openai::get_instance().get_attributes().get_name(), Openai::get_instance(), BROOKESIA_AGENT_OPENAI_PLUGIN_SYMBOL
);
#endif

} // namespace esp_brookesia::agent
