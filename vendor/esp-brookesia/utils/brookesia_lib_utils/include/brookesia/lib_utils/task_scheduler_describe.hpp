/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/lib_utils/task_scheduler.hpp"
#include "brookesia/lib_utils/thread_config_describe.hpp"

namespace esp_brookesia::lib_utils {

BROOKESIA_DESCRIBE_ENUM(TaskScheduler::TaskType, Immediate, Delayed, Periodic)
BROOKESIA_DESCRIBE_ENUM(TaskScheduler::TaskState, Running, Suspended, Canceled, Finished)
BROOKESIA_DESCRIBE_STRUCT(
    TaskScheduler::Statistics,
    (),
    (total_tasks, completed_tasks, failed_tasks, canceled_tasks, suspended_tasks)
)
BROOKESIA_DESCRIBE_STRUCT(TaskScheduler::GroupConfig, (), (enable_serial_execution, parent_group))
BROOKESIA_DESCRIBE_STRUCT(
    TaskScheduler::StartConfig,
    (),
    (worker_configs, worker_poll_interval_ms, pre_execute_callback, post_execute_callback)
)

} // namespace esp_brookesia::lib_utils
