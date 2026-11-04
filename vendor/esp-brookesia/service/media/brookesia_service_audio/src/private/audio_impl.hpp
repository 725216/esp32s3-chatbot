#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "boost/format.hpp"
#include "brookesia/service_audio/macro_configs.h"
#if !BROOKESIA_SERVICE_AUDIO_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/dataflow_provider.hpp"
#include "private/utils.hpp"
#include "brookesia/lib_utils/plugin.hpp"
#include "brookesia/hal_interface/interface.hpp"
#include "brookesia/hal_interface/interfaces/audio/processor.hpp"
#include "brookesia/service_audio/service_audio.hpp"
#include "brookesia/service_manager/dataflow/registry.hpp"
#include "brookesia/service_manager/service/manager.hpp"

namespace esp_brookesia::service {

constexpr size_t ENCODER_FETCH_DATA_SIZE_MORE = 100;
constexpr const char *DECODER_OUTPUT_NAME = "Speaker0";
constexpr const char *DECODER_OUTPUT_ROLE = "speaker";
constexpr uint32_t DECODER_STREAM_DRAIN_INTERVAL_MS = 1;
constexpr uint32_t DECODER_STREAM_QUEUE_SIZE_DEFAULT = 32 * 1024;
/* LOCAL FIX (2026-10-31): 播放器侧预卷 v13——v12 重采样跨帧修复后用户实测
 * "后面可以"，仅剩"前面卡"=预卷等待期静音空白太长(64KB≈2.1s)。
 * v9 日志逐帧模拟证明到达率>1.0(队列从开播 2.35s 一路涨到 10s+)，48KB
 * 起步足够吸收开头大间隔(864/782ms)：开播等待~1.0s，队列 1.73s 起步。
 * 超时 1500ms 兜底短回复/服务器停顿。 */
/* v18 (2026-11-03): 豆包 duplex 流式每块 0.5s(20-27KB) 音频，96KB 目标永远攒不满，
 * 导致总走"preload timeout play now"只剩 ~0.23s 缓冲硬播→播完立刻断(断续/吞字)。
 * 降到 20KB≈1 块：首块到即 full play，开播缓冲翻倍、瞬断减轻。不影响小智整句下发。 */
constexpr uint32_t DECODER_STREAM_PRELOAD_BYTES = 20 * 1024;
/* v19 (2026-11-03): 串口日志实测豆包卡顿根因——2s 超时太短，服务端大音频包
 * (16-27KB)到达间隔 0.3-0.75s，2s 内常只收到 10-13KB 就超时硬播→断续。
 * 增到 5s：给足时间攒到 20KB 目标或至少 2+ 块数据，大幅减少超时硬播。 */
constexpr uint32_t DECODER_STREAM_PRELOAD_TIMEOUT_TICKS = 5000;
/* 回合内宽限期：drained(帧间真空)后 GRACE 内来数据视为同一回合续播，
 * 不重新预卷(否则分片喂入下每帧之间都重预卷→播放被切成断续)；超过 GRACE
 * 仍无数据视为回合结束，下回合数据到达时重新预卷。 */
/* v21 (2026-11-03): 豆包 TTS 分段合成，音频块间隔 10-30s（长回复期间服务端
 * 合成下一段），5000 tick grace 不够用→过期→重新 armed→等 20KB/5s 超时→卡顿。
 * 增到 15000(≈30s)：覆盖绝大多数 TTS 分段间隔，回合内不重预卷直接续播。 */
constexpr uint32_t DECODER_STREAM_PRELOAD_GRACE_TICKS = 15000;
constexpr size_t DECODER_STREAM_TAIL_BUFFER_BYTES = 32 * 1024;  /* 1.0s @16k mono */
constexpr int DECODER_STREAM_TAIL_STRETCH_MAX_ROUNDS = 3;       /* 每轮 1.25s，3 轮≈3.75s */
constexpr size_t DECODER_STREAM_TAIL_STRETCH_OUT_BYTES = 40 * 1024; /* 1.25x 重采样输出 */
/* v15 (2026-11-03): 用户实测豆包回答"中间一顿一顿"，即块间 0.3-0.75s 真空中
 * <400ms 的部分走静音过渡→听感断断续续。将阈值降到 200ms：200-400ms 的短真空
 * 也走尾音拉伸覆盖，减少"顿一下"；仍保留 ≥200ms 才拉伸，防更短抖动/整段静音。
 * v13 的开头拖音问题已由 96KB 预卷吸收，播放中短真空拉伸相对安全。 */
/* v20 (2026-11-03): 用户反馈"还有轻微卡顿"——200 tick(≈200ms)阈值太高，
 * 豆包块间 300-750ms 真空前 200ms 走纯静音→听感"顿一下"才进拉伸。
 * 降到 30(≈30ms)：真空几乎立刻进尾音拉伸平滑过渡，消除静音间隙。 */
constexpr uint32_t DECODER_STREAM_UNDERRUN_STRETCH_MIN_TICKS = 30;

namespace {

inline int64_t get_current_time_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// The registries below are intentionally leaked (heap-allocated, never deleted) to
// avoid a static destruction order fiasco: AudioDecoder/AudioEncoder instances are
// owned by the ServiceBase PluginRegistry (constructed during static init, destroyed
// late), while these function-local statistics are constructed at runtime and would
// otherwise be destroyed first. Their destructors would then access already-freed
// registries at program exit (heap-use-after-free).
std::mutex &get_decoder_registry_mutex()
{
    static std::mutex *mutex = new std::mutex();
    return *mutex;
}

std::vector<AudioDecoder *> &get_decoder_registry()
{
    static std::vector<AudioDecoder *> *registry = new std::vector<AudioDecoder *>();
    return *registry;
}

std::mutex &get_encoder_registry_mutex()
{
    static std::mutex *mutex = new std::mutex();
    return *mutex;
}

std::vector<AudioEncoder *> &get_encoder_registry()
{
    static std::vector<AudioEncoder *> *registry = new std::vector<AudioEncoder *>();
    return *registry;
}

std::string make_dataflow_provider_id(std::string_view model, int id)
{
    return "audio." + std::string(model) + "." + std::to_string(id);
}

} // namespace


} // namespace esp_brookesia::service

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
