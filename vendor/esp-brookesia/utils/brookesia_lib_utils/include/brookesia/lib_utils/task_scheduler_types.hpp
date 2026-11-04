/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "brookesia/lib_utils/thread_config.hpp"

namespace esp_brookesia::lib_utils {

using TaskSchedulerTaskId = uint64_t;
using TaskSchedulerGroup = std::string;

enum class TaskSchedulerTaskType {
    Immediate,
    Delayed,
    Periodic,
};

enum class TaskSchedulerTaskState {
    Running,
    Suspended,
    Canceled,
    Finished,
};

struct TaskSchedulerStatistics {
    size_t total_tasks = 0;
    size_t completed_tasks = 0;
    size_t failed_tasks = 0;
    size_t canceled_tasks = 0;
    size_t suspended_tasks = 0;
};

using TaskSchedulerPreExecuteCallback =
    std::function<void(const TaskSchedulerGroup &, TaskSchedulerTaskId, TaskSchedulerTaskType)>;
using TaskSchedulerPostExecuteCallback =
    std::function<void(const TaskSchedulerGroup &, TaskSchedulerTaskId, TaskSchedulerTaskType, bool)>;

struct TaskSchedulerStartConfig {
    std::vector<ThreadConfig> worker_configs{
        ThreadConfig{
            .name = "Worker",
            .stack_size = 6 * 1024,
        }
    };
    size_t worker_poll_interval_ms = 10;
    TaskSchedulerPreExecuteCallback pre_execute_callback = nullptr;
    TaskSchedulerPostExecuteCallback post_execute_callback = nullptr;
};

struct TaskSchedulerGroupConfig {
    bool enable_serial_execution = false;
    TaskSchedulerGroup parent_group = "";
    TaskSchedulerPreExecuteCallback pre_execute_callback = nullptr;
    TaskSchedulerPostExecuteCallback post_execute_callback = nullptr;
};

} // namespace esp_brookesia::lib_utils
