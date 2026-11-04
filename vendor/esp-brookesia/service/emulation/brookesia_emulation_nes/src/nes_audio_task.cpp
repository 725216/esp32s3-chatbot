/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/nes_impl.hpp"

namespace esp_brookesia::emulation {

bool Nes::audio_task()
{
    AudioStepStats stats;
    return audio_step(&stats);
}

bool Nes::frame_step(bool draw_video, FrameStepStats *stats)
{
    std::shared_ptr<Runtime> runtime;
    {
        std::lock_guard lock(mutex_);
        if ((state_ != State::Running) || !runtime_) {
            return false;
        }
        runtime = runtime_;
    }

    if (!runtime->step_frame(draw_video, stats)) {
        std::lock_guard lock(mutex_);
        set_state(State::Error);
        publish_error("NES frame step failed");
        return false;
    }
    return true;
}

bool Nes::audio_step(AudioStepStats *stats)
{
    std::shared_ptr<Runtime> runtime;
    {
        std::lock_guard lock(mutex_);
        if ((state_ != State::Running) || !runtime_) {
            return false;
        }
        runtime = runtime_;
    }

    const bool result = runtime->flush_audio_queue(stats);
    if (stats != nullptr) {
        std::lock_guard lock(perf_mutex_);
        audio_chunks_since_log_ += stats->fed_chunks;
        audio_max_feed_ms_ = std::max(audio_max_feed_ms_, stats->elapsed_ms);
    }
    return result;
}

} // namespace esp_brookesia::emulation
