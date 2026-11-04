/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/nes_impl.hpp"

namespace esp_brookesia::emulation {

Nes::Nes()
    : service::ServiceBase({
    .name = Helper::get_name().data(),
    .description = "Run NES emulation and expose emulator controls.",
    .version = service::ServiceBase::make_version(
        BROOKESIA_EMULATION_NES_VER_MAJOR, BROOKESIA_EMULATION_NES_VER_MINOR,
        BROOKESIA_EMULATION_NES_VER_PATCH
    ),
#if BROOKESIA_EMULATION_NES_ENABLE_WORKER
    .task_scheduler_config = lib_utils::TaskScheduler::StartConfig{
        .worker_configs = {
            lib_utils::ThreadConfig{
                .name = BROOKESIA_EMULATION_NES_WORKER_NAME "0",
                .core_id = BROOKESIA_EMULATION_NES_WORKER_0_CORE_ID,
                .priority = BROOKESIA_EMULATION_NES_WORKER_PRIORITY,
                .stack_size = BROOKESIA_EMULATION_NES_WORKER_STACK_SIZE,
                .stack_in_ext = BROOKESIA_EMULATION_NES_WORKER_STACK_IN_EXT,
            },
            lib_utils::ThreadConfig{
                .name = BROOKESIA_EMULATION_NES_WORKER_NAME "1",
                .core_id = BROOKESIA_EMULATION_NES_WORKER_1_CORE_ID,
                .priority = BROOKESIA_EMULATION_NES_WORKER_PRIORITY,
                .stack_size = BROOKESIA_EMULATION_NES_WORKER_STACK_SIZE,
                .stack_in_ext = BROOKESIA_EMULATION_NES_WORKER_STACK_IN_EXT,
            },
        },
        .worker_poll_interval_ms = BROOKESIA_EMULATION_NES_WORKER_POLL_INTERVAL_MS,
    },
#endif
})
{
}

Nes::~Nes() = default;

bool Nes::on_init()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
    BROOKESIA_LOGI(
        "Version: %1%.%2%.%3%", BROOKESIA_EMULATION_NES_VER_MAJOR, BROOKESIA_EMULATION_NES_VER_MINOR,
        BROOKESIA_EMULATION_NES_VER_PATCH
    );

    runtime_ = std::make_shared<Runtime>();
    return true;
}

bool Nes::on_start()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
    auto scheduler = get_task_scheduler();
    BROOKESIA_CHECK_NULL_RETURN(scheduler, false, "Task scheduler is not available");
    BROOKESIA_CHECK_FALSE_RETURN(scheduler->is_running(), false, "Task scheduler is not running");
    BROOKESIA_CHECK_FALSE_RETURN(
    scheduler->configure_group(NES_FRAME_TASK_GROUP, {
        .enable_serial_execution = true,
    }), false, "Failed to configure NES frame task group"
    );
    BROOKESIA_CHECK_FALSE_RETURN(
    scheduler->configure_group(NES_AUDIO_TASK_GROUP, {
        .enable_serial_execution = true,
    }), false, "Failed to configure NES audio task group"
    );
    return true;
}

void Nes::on_stop()
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
}

void Nes::on_deinit()
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

    {
        std::lock_guard lock(mutex_);
        if (runtime_) {
            runtime_->unload();
        }
        runtime_.reset();
        config_ = {};
        state_ = State::Idle;
    }
    BROOKESIA_LOGI("Deinitialized");
}

} // namespace esp_brookesia::emulation
