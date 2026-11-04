/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/nes_impl.hpp"

namespace esp_brookesia::emulation {

bool Nes::start_frame_task_locked()
{
    if (frame_task_id_ != 0) {
        return true;
    }
    auto scheduler = get_task_scheduler();
    BROOKESIA_CHECK_NULL_RETURN(scheduler, false, "Task scheduler is not available");
    BROOKESIA_CHECK_FALSE_RETURN(scheduler->is_running(), false, "Task scheduler is not running");

    frame_late_count_ = 0;
    frame_count_since_log_ = 0;
    frame_draw_count_since_log_ = 0;
    frame_skip_count_since_log_ = 0;
    frame_max_step_ms_ = 0;
    frame_total_step_ms_ = 0;
    frame_draw_step_ms_ = 0;
    frame_max_emulate_ms_ = 0;
    frame_max_convert_ms_ = 0;
    frame_max_present_ms_ = 0;
    frame_max_late_by_ms_ = 0;
    audio_chunks_since_log_ = 0;
    audio_max_feed_ms_ = 0;
    consecutive_frame_skip_count_ = 0;
    skip_next_video_frame_ = false;
    base_frame_skip_ = std::min<uint32_t>(BROOKESIA_EMULATION_NES_BASE_FRAME_SKIP, get_auto_frame_skip_max());
    current_frame_skip_ = base_frame_skip_;
    frame_skip_remaining_ = 0;
    frame_backpressure_count_since_log_ = 0;
    next_frame_deadline_ms_ = get_current_time_ms();
    next_perf_log_ms_ = get_current_time_ms() + BROOKESIA_EMULATION_NES_PERF_LOG_INTERVAL_MS;

    lib_utils::TaskScheduler::TaskId task_id = 0;
    auto task = [this]() -> bool {
        return frame_task();
    };
    if (!scheduler->post_periodic(
                std::move(task), static_cast<int>(BROOKESIA_EMULATION_NES_FRAME_INTERVAL_MS), &task_id,
                NES_FRAME_TASK_GROUP
            )) {
        BROOKESIA_LOGE("Failed to schedule NES frame task");
        return false;
    }
    frame_task_id_ = task_id;
    return true;
}

bool Nes::start_audio_task_locked()
{
    if (audio_task_id_ != 0) {
        return true;
    }
    auto scheduler = get_task_scheduler();
    BROOKESIA_CHECK_NULL_RETURN(scheduler, false, "Task scheduler is not available");
    BROOKESIA_CHECK_FALSE_RETURN(scheduler->is_running(), false, "Task scheduler is not running");

    lib_utils::TaskScheduler::TaskId task_id = 0;
    auto task = [this]() -> bool {
        return audio_task();
    };
    if (!scheduler->post_periodic(
                std::move(task), static_cast<int>(NES_AUDIO_FEED_INTERVAL_MS), &task_id, NES_AUDIO_TASK_GROUP
            )) {
        BROOKESIA_LOGE("Failed to schedule NES audio task");
        return false;
    }
    audio_task_id_ = task_id;
    return true;
}

void Nes::take_task_ids_locked(std::vector<lib_utils::TaskScheduler::TaskId> &task_ids)
{
    if (audio_task_id_ != 0) {
        task_ids.push_back(audio_task_id_);
        audio_task_id_ = 0;
    }
    if (frame_task_id_ != 0) {
        task_ids.push_back(frame_task_id_);
        frame_task_id_ = 0;
    }
}

void Nes::cancel_and_wait_task_ids(const std::vector<lib_utils::TaskScheduler::TaskId> &task_ids)
{
    if (task_ids.empty()) {
        return;
    }
    auto scheduler = get_task_scheduler();
    if (!scheduler || !scheduler->is_running()) {
        return;
    }
    for (const auto task_id : task_ids) {
        scheduler->cancel(task_id);
    }
    for (const auto task_id : task_ids) {
        if (!scheduler->wait(task_id, NES_TASK_STOP_TIMEOUT_MS)) {
            BROOKESIA_LOGW("Timed out waiting for NES task %1% to stop", task_id);
        }
    }
}

bool Nes::frame_task()
{
    using Clock = std::chrono::steady_clock;

    const auto now_ms = get_current_time_ms();
    if ((next_frame_deadline_ms_ > 0) && (now_ms < next_frame_deadline_ms_)) {
        return true;
    }

    const uint32_t late_by_ms = (next_frame_deadline_ms_ > 0 && now_ms > next_frame_deadline_ms_) ?
                                static_cast<uint32_t>(now_ms - next_frame_deadline_ms_) : 0;
    std::shared_ptr<Runtime> runtime;
    {
        std::lock_guard lock(mutex_);
        runtime = runtime_;
    }
    const bool display_backpressure = runtime && runtime->consume_display_backpressure();
    const uint32_t max_frame_skip = get_auto_frame_skip_max();
    const bool scheduled_skip = frame_skip_remaining_ > 0;
    const bool can_skip_video = max_frame_skip > 0 && consecutive_frame_skip_count_ < max_frame_skip;
    const bool draw_video = !(can_skip_video &&
                              (scheduled_skip || skip_next_video_frame_ || display_backpressure ||
                               (late_by_ms > BROOKESIA_EMULATION_NES_FRAME_PERIOD_MS)));

    const auto step_start = Clock::now();
    FrameStepStats stats;
    if (!frame_step(draw_video, &stats)) {
        return false;
    }
    const bool drew_video = stats.drew_video;

    const auto step_elapsed_ms = static_cast<uint32_t>(
                                     std::chrono::duration_cast<std::chrono::milliseconds>(
                                         Clock::now() - step_start
                                     ).count()
                                 );
    if (drew_video) {
        consecutive_frame_skip_count_ = 0;
        frame_skip_remaining_ = current_frame_skip_;
    } else {
        consecutive_frame_skip_count_++;
        if (frame_skip_remaining_ > 0) {
            frame_skip_remaining_--;
        }
    }
    skip_next_video_frame_ = drew_video && (step_elapsed_ms > BROOKESIA_EMULATION_NES_FRAME_PERIOD_MS);
    next_frame_deadline_ms_ = (next_frame_deadline_ms_ > 0) ?
                              (next_frame_deadline_ms_ + BROOKESIA_EMULATION_NES_FRAME_PERIOD_MS) :
                              (now_ms + BROOKESIA_EMULATION_NES_FRAME_PERIOD_MS);
    const auto after_step_ms = get_current_time_ms();
    if (next_frame_deadline_ms_ + BROOKESIA_EMULATION_NES_FRAME_PERIOD_MS < after_step_ms) {
        next_frame_deadline_ms_ = after_step_ms + BROOKESIA_EMULATION_NES_FRAME_PERIOD_MS;
    }

    {
        std::lock_guard lock(perf_mutex_);
        frame_max_step_ms_ = std::max(frame_max_step_ms_, step_elapsed_ms);
        frame_max_emulate_ms_ = std::max(frame_max_emulate_ms_, stats.emulate_ms);
        frame_max_convert_ms_ = std::max(frame_max_convert_ms_, stats.convert_ms);
        frame_max_present_ms_ = std::max(frame_max_present_ms_, stats.present_ms);
        frame_max_late_by_ms_ = std::max(frame_max_late_by_ms_, late_by_ms);
        frame_count_since_log_++;
        frame_total_step_ms_ += step_elapsed_ms;
        if (drew_video) {
            frame_draw_count_since_log_++;
            frame_draw_step_ms_ += step_elapsed_ms;
        } else {
            frame_skip_count_since_log_++;
        }
        if (display_backpressure) {
            frame_backpressure_count_since_log_++;
        }
        if ((step_elapsed_ms > BROOKESIA_EMULATION_NES_FRAME_PERIOD_MS) ||
                (late_by_ms > BROOKESIA_EMULATION_NES_FRAME_PERIOD_MS)) {
            frame_late_count_++;
        }
    }

    if constexpr (BROOKESIA_EMULATION_NES_PERF_LOG_INTERVAL_MS > 0) {
        const auto perf_now_ms = get_current_time_ms();
        if (perf_now_ms >= next_perf_log_ms_) {
            std::shared_ptr<Runtime> runtime;
            {
                std::lock_guard lock(mutex_);
                runtime = runtime_;
            }
            auto perf = runtime ? runtime->get_perf_snapshot() : Runtime::PerfSnapshot{};
            uint32_t frames = 0;
            uint32_t max_step = 0;
            uint32_t max_emulate = 0;
            uint32_t max_convert = 0;
            uint32_t max_present = 0;
            uint32_t max_late_by = 0;
            uint32_t late = 0;
            uint32_t drawn = 0;
            uint32_t skipped = 0;
            uint32_t avg_step = 0;
            uint32_t avg_draw_step = 0;
            uint32_t audio_chunks = 0;
            uint32_t audio_max_feed = 0;
            uint32_t backpressure = 0;
            uint32_t frame_skip_level = 0;
            {
                std::lock_guard lock(perf_mutex_);
                frames = frame_count_since_log_;
                max_step = frame_max_step_ms_;
                max_emulate = frame_max_emulate_ms_;
                max_convert = frame_max_convert_ms_;
                max_present = frame_max_present_ms_;
                max_late_by = frame_max_late_by_ms_;
                late = frame_late_count_;
                drawn = frame_draw_count_since_log_;
                skipped = frame_skip_count_since_log_;
                avg_step = frames > 0 ? static_cast<uint32_t>(frame_total_step_ms_ / frames) : 0;
                avg_draw_step = frame_draw_count_since_log_ > 0 ?
                                static_cast<uint32_t>(frame_draw_step_ms_ / frame_draw_count_since_log_) : 0;
                audio_chunks = audio_chunks_since_log_;
                audio_max_feed = audio_max_feed_ms_;
                backpressure = frame_backpressure_count_since_log_;
                frame_skip_level = current_frame_skip_;
                frame_count_since_log_ = 0;
                frame_max_step_ms_ = 0;
                frame_max_emulate_ms_ = 0;
                frame_max_convert_ms_ = 0;
                frame_max_present_ms_ = 0;
                frame_max_late_by_ms_ = 0;
                frame_late_count_ = 0;
                frame_draw_count_since_log_ = 0;
                frame_skip_count_since_log_ = 0;
                frame_total_step_ms_ = 0;
                frame_draw_step_ms_ = 0;
                frame_backpressure_count_since_log_ = 0;
                audio_chunks_since_log_ = 0;
                audio_max_feed_ms_ = 0;
            }
            const bool should_raise_frame_skip = frames > 0 &&
                                                 ((late * 10 >= frames) ||
                                                  (backpressure * 10 >= frames) ||
                                                  (avg_draw_step > (BROOKESIA_EMULATION_NES_FRAME_PERIOD_MS + 2)));
            const bool should_lower_frame_skip = frames > 0 &&
                                                 (late * 100 <= frames * 2) &&
                                                 (backpressure * 100 <= frames * 2) &&
                                                 (avg_draw_step <= BROOKESIA_EMULATION_NES_FRAME_PERIOD_MS);
            if (should_raise_frame_skip && (current_frame_skip_ < max_frame_skip)) {
                current_frame_skip_++;
            } else if (should_lower_frame_skip && (current_frame_skip_ > base_frame_skip_)) {
                current_frame_skip_--;
            }
            std::string audio_state = "muted";
            if (perf.audio_mode == AudioMode::Disabled) {
                audio_state = "disabled";
            } else if (perf.audio_started) {
                audio_state = "enabled";
            }
            BROOKESIA_LOGI(
                "NES perf: frames=%1%, drawn=%2%, skip=%3%, late=%4%, avg_step=%5% ms, avg_draw=%6% ms, "
                "max_step=%7% ms, max_emulate=%8% ms, max_convert=%9% ms, max_present=%10% ms, "
                "max_late_by=%11% ms, audio=%12%, audio_chunks=%13%, audio_max_feed=%14% ms, "
                "audio_depth=%15%, audio_drop=%16%, render=%17%x%18%, video_mode=%19%, frame_skip=%20%->%21%, "
                "backpressure=%22%, present_drop=%23%, present_error=%24%",
                frames, drawn, skipped, late, avg_step, avg_draw_step, max_step, max_emulate, max_convert,
                max_present, max_late_by, audio_state, audio_chunks, audio_max_feed, perf.audio_queue_depth,
                perf.audio_drop_count, perf.render_width, perf.render_height,
                BROOKESIA_DESCRIBE_ENUM_TO_STR(perf.video_mode), frame_skip_level, current_frame_skip_, backpressure,
                perf.present_drop_count, perf.present_error_count
            );
            next_perf_log_ms_ = perf_now_ms + BROOKESIA_EMULATION_NES_PERF_LOG_INTERVAL_MS;
        }
    }
    return true;
}


} // namespace esp_brookesia::emulation
