/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/nes_impl.hpp"

namespace esp_brookesia::emulation {

std::expected<void, std::string> Nes::load(Config config)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
    BROOKESIA_LOGD("Params: config(%1%)", config);

    std::vector<lib_utils::TaskScheduler::TaskId> task_ids;
    {
        std::lock_guard lock(mutex_);
        if (!runtime_) {
            return std::unexpected("NES runtime is not initialized");
        }
        take_task_ids_locked(task_ids);
        if ((state_ == State::Running) || (state_ == State::Paused)) {
            set_state(State::Stopped);
        }
    }

    cancel_and_wait_task_ids(task_ids);

    std::lock_guard lock(mutex_);
    runtime_->unload();
    auto result = runtime_->load(config);
    if (!result) {
        set_state(State::Error);
        publish_error(result.error());
        return result;
    }
    config_ = std::move(config);
    set_state(State::Loaded);
    return {};
}

std::expected<void, std::string> Nes::start()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::vector<lib_utils::TaskScheduler::TaskId> task_ids;
    bool failed_to_start_frame = false;
    {
        std::lock_guard lock(mutex_);
        if ((state_ != State::Loaded) && (state_ != State::Paused) && (state_ != State::Stopped)) {
            return std::unexpected("NES runtime is not loaded");
        }
        set_state(State::Running);
        if (runtime_ && runtime_->is_audio_started() && !start_audio_task_locked()) {
            BROOKESIA_LOGW("Failed to start NES audio task; continuing without decoupled audio feed");
        }
        if (!start_frame_task_locked()) {
            take_task_ids_locked(task_ids);
            set_state(State::Error);
            failed_to_start_frame = true;
        }
    }
    cancel_and_wait_task_ids(task_ids);
    if (failed_to_start_frame) {
        return std::unexpected("Failed to start NES frame task");
    }
    return {};
}

std::expected<void, std::string> Nes::pause()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::vector<lib_utils::TaskScheduler::TaskId> task_ids;
    {
        std::lock_guard lock(mutex_);
        if (state_ != State::Running) {
            return {};
        }
        take_task_ids_locked(task_ids);
        set_state(State::Paused);
    }
    cancel_and_wait_task_ids(task_ids);
    return {};
}

std::expected<void, std::string> Nes::resume()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    return start();
}

std::expected<void, std::string> Nes::stop()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::vector<lib_utils::TaskScheduler::TaskId> task_ids;
    {
        std::lock_guard lock(mutex_);
        take_task_ids_locked(task_ids);
        if ((state_ == State::Running) || (state_ == State::Paused)) {
            set_state(State::Stopped);
        }
    }
    cancel_and_wait_task_ids(task_ids);

    std::lock_guard lock(mutex_);
    if (runtime_) {
        runtime_->unload();
    }
    config_ = {};
    set_state(State::Stopped);
    return {};
}

std::expected<void, std::string> Nes::reset()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(mutex_);
    if (!runtime_) {
        return std::unexpected("NES runtime is not initialized");
    }
    return runtime_->reset();
}

std::expected<void, std::string> Nes::save()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(mutex_);
    if (!runtime_) {
        return std::unexpected("NES runtime is not initialized");
    }
    auto result = runtime_->save();
    if (!result) {
        publish_error(result.error());
        return result;
    }
    if (!config_.save_path.empty()) {
        publish_event(
            BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::SaveCompleted),
        {service::EventItem(config_.save_path)}
        );
    }
    return {};
}

std::expected<void, std::string> Nes::set_gamepad_state(GamepadState state)
{
    std::lock_guard lock(mutex_);
    if (!runtime_) {
        return std::unexpected("NES runtime is not initialized");
    }
    return runtime_->set_gamepad_state(state);
}

Nes::State Nes::get_state() const
{
    std::lock_guard lock(mutex_);
    return state_;
}

} // namespace esp_brookesia::emulation
