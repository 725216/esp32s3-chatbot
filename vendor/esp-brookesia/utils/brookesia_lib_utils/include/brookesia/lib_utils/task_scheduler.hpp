/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include <vector>

#include "brookesia/lib_utils/task_scheduler_types.hpp"

namespace esp_brookesia::lib_utils {

/**
 * @brief Asynchronous task scheduler.
 *
 * Platform-specific executor, timer, future, and thread types are intentionally hidden
 * behind the implementation object so users of the scheduler do not parse its backend.
 */
class TaskScheduler {
public:
    using TaskId = TaskSchedulerTaskId;
    using OnceTask = std::function<void()>;
    using PeriodicTask = std::function<bool()>;
    using Group = TaskSchedulerGroup;
    using TaskType = TaskSchedulerTaskType;
    using TaskState = TaskSchedulerTaskState;
    using Statistics = TaskSchedulerStatistics;
    using PreExecuteCallback = TaskSchedulerPreExecuteCallback;
    using PostExecuteCallback = TaskSchedulerPostExecuteCallback;
    using StartConfig = TaskSchedulerStartConfig;
    using GroupConfig = TaskSchedulerGroupConfig;

    /** @brief Construct an idle task scheduler. */
    TaskScheduler();

    /** @brief Stop the scheduler and release all backend resources. */
    ~TaskScheduler();

    TaskScheduler(const TaskScheduler &) = delete;
    TaskScheduler &operator=(const TaskScheduler &) = delete;
    TaskScheduler(TaskScheduler &&) = delete;
    TaskScheduler &operator=(TaskScheduler &&) = delete;

    /** @brief Start worker execution with explicit configuration. */
    bool start(const StartConfig &config);

    /** @brief Start worker execution with the default configuration. */
    bool start();

    /** @brief Stop workers and cancel pending tasks. */
    void stop();

    /** @brief Check whether the scheduler is running. */
    bool is_running() const;

    /** @brief Check whether the caller is one of this scheduler's workers. */
    bool is_current_thread_worker() const;

    /** @brief Check whether the caller is running through a configured task group. */
    bool is_current_thread_in_group(const Group &group) const;

    /** @brief Reserve worker capacity while the current worker blocks for a result. */
    bool try_acquire_worker_wait_slot();

    /** @brief Release a worker wait slot held by the current worker. */
    void release_worker_wait_slot();

    /** @brief Configure serialized execution and callbacks for a task group. */
    bool configure_group(const Group &group, const GroupConfig &config);

    /** @brief Dispatch immediately when the current execution context permits it. */
    bool dispatch(OnceTask task, TaskId *id = nullptr, const Group &group = {});

    /** @brief Post a one-shot task. */
    bool post(OnceTask task, TaskId *id = nullptr, const Group &group = {});

    /** @brief Post a one-shot task after a delay in milliseconds. */
    bool post_delayed(OnceTask task, int delay_ms, TaskId *id = nullptr, const Group &group = {});

    /** @brief Post a periodic task with an interval in milliseconds. */
    bool post_periodic(PeriodicTask task, int interval_ms, TaskId *id = nullptr, const Group &group = {});

    /** @brief Post multiple one-shot tasks to the same group. */
    bool post_batch(std::vector<OnceTask> tasks, std::vector<TaskId> *ids = nullptr, const Group &group = {});

    /** @brief Cancel a task. */
    void cancel(TaskId id);

    /** @brief Cancel every task in a group. */
    void cancel_group(const Group &group);

    /** @brief Cancel all tracked tasks. */
    void cancel_all();

    /** @brief Suspend a delayed or periodic task. */
    bool suspend(TaskId id);

    /** @brief Suspend all suspendable tasks in a group. */
    size_t suspend_group(const Group &group);

    /** @brief Suspend all suspendable tasks. */
    size_t suspend_all();

    /** @brief Resume a suspended task. */
    bool resume(TaskId id);

    /** @brief Resume all suspended tasks in a group. */
    size_t resume_group(const Group &group);

    /** @brief Resume all suspended tasks. */
    size_t resume_all();

    /** @brief Wait for one task, or indefinitely when timeout is negative. */
    bool wait(TaskId id, int timeout_ms = -1);

    /** @brief Wait for every task in a group. */
    bool wait_group(const Group &group, int timeout_ms = -1);

    /** @brief Wait for every tracked task. */
    bool wait_all(int timeout_ms = -1);

    /** @brief Restart the countdown of a running delayed or periodic task. */
    bool restart_timer(TaskId id);

    /** @brief Get the type of a task. */
    TaskType get_type(TaskId id) const;

    /** @brief Get the state of a task. */
    TaskState get_state(TaskId id) const;

    /** @brief Get the group assigned to a task. */
    Group get_group(TaskId id) const;

    /** @brief Get the number of tracked tasks in a group. */
    size_t get_group_task_count(const Group &group) const;

    /** @brief Get all groups that currently contain tasks. */
    std::vector<Group> get_active_groups() const;

    /** @brief Get accumulated task statistics. */
    Statistics get_statistics() const;

    /** @brief Get the number of active worker threads. */
    size_t get_worker_count() const;

    /** @brief Reset accumulated task statistics. */
    void reset_statistics();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace esp_brookesia::lib_utils
