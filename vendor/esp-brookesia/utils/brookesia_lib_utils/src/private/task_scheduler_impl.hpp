/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <unordered_set>
#include <vector>

#include "boost/thread/future.hpp"
#include "boost/thread/mutex.hpp"
#include "boost/thread/thread.hpp"
#include "brookesia/lib_utils/macro_configs.h"
#include "brookesia/lib_utils/task_scheduler.hpp"

#if defined(CONFIG_BROOKESIA_LIB_UTILS_WASM_SINGLE_THREAD_SCHEDULER) && \
    CONFIG_BROOKESIA_LIB_UTILS_WASM_SINGLE_THREAD_SCHEDULER
#   define BROOKESIA_LIB_UTILS_USE_WASM_SINGLE_THREAD_SCHEDULER 1
#else
#   define BROOKESIA_LIB_UTILS_USE_WASM_SINGLE_THREAD_SCHEDULER 0
#endif

#if !BROOKESIA_LIB_UTILS_USE_WASM_SINGLE_THREAD_SCHEDULER
#   include "boost/asio/executor_work_guard.hpp"
#   include "boost/asio/io_context.hpp"
#   include "boost/asio/steady_timer.hpp"
#   include "boost/asio/strand.hpp"
#endif

namespace esp_brookesia::lib_utils {

class TaskScheduler::Impl {
public:
    using TaskId = TaskScheduler::TaskId;
    using OnceTask = TaskScheduler::OnceTask;
    using PeriodicTask = TaskScheduler::PeriodicTask;
    using Group = TaskScheduler::Group;
    using TaskType = TaskScheduler::TaskType;
    using TaskState = TaskScheduler::TaskState;
    using Statistics = TaskScheduler::Statistics;
    using PreExecuteCallback = TaskScheduler::PreExecuteCallback;
    using PostExecuteCallback = TaskScheduler::PostExecuteCallback;
    using StartConfig = TaskScheduler::StartConfig;
    using GroupConfig = TaskScheduler::GroupConfig;

    struct TaskHandle {
        TaskId id = 0;
#if BROOKESIA_LIB_UTILS_USE_WASM_SINGLE_THREAD_SCHEDULER
        std::atomic<uint64_t> generation = 0;
#else
        std::shared_ptr<boost::asio::steady_timer> timer;
#endif
        std::atomic<TaskState> state = TaskState::Running;
        TaskType type = TaskType::Immediate;
        bool repeat = false;
        int interval_ms = 0;
        Group group;
        std::shared_ptr<boost::promise<bool>> promise;
        boost::shared_future<bool> future;
        std::atomic<bool> is_executing = false;
        std::chrono::steady_clock::time_point suspend_time;
        std::chrono::milliseconds remaining_time = std::chrono::milliseconds(0);
        OnceTask saved_task;
        PeriodicTask saved_periodic_task;
    };

#if BROOKESIA_LIB_UTILS_USE_WASM_SINGLE_THREAD_SCHEDULER
    struct WasmTimerContext {
        Impl *scheduler = nullptr;
        std::shared_ptr<TaskHandle> handle;
        OnceTask once_task;
        PeriodicTask periodic_task;
        uint64_t generation = 0;
    };
#endif

    Impl() = default;
    ~Impl();

    bool start(const StartConfig &config);
    void stop();
    bool is_running() const
    {
        return is_running_.load();
    }
    bool is_current_thread_worker() const;
    bool is_current_thread_in_group(const Group &group) const;
    bool try_acquire_worker_wait_slot();
    void release_worker_wait_slot();
    bool configure_group(const Group &group, const GroupConfig &config);
    bool dispatch(OnceTask task, TaskId *id, const Group &group);
    bool post(OnceTask task, TaskId *id, const Group &group);
    bool post_delayed(OnceTask task, int delay_ms, TaskId *id, const Group &group);
    bool post_periodic(PeriodicTask task, int interval_ms, TaskId *id, const Group &group);
    bool post_batch(std::vector<OnceTask> tasks, std::vector<TaskId> *ids, const Group &group);
    void cancel(TaskId id);
    void cancel_group(const Group &group);
    void cancel_all();
    bool suspend(TaskId id);
    size_t suspend_group(const Group &group);
    size_t suspend_all();
    bool resume(TaskId id);
    size_t resume_group(const Group &group);
    size_t resume_all();
    bool wait(TaskId id, int timeout_ms);
    bool wait_group(const Group &group, int timeout_ms);
    bool wait_all(int timeout_ms);
    bool restart_timer(TaskId id);
    TaskType get_type(TaskId id) const;
    TaskState get_state(TaskId id) const;
    Group get_group(TaskId id) const;
    size_t get_group_task_count(const Group &group) const;
    std::vector<Group> get_active_groups() const;
    Statistics get_statistics() const;
    size_t get_worker_count() const
    {
        boost::lock_guard lock(mutex_);
#if BROOKESIA_LIB_UTILS_USE_WASM_SINGLE_THREAD_SCHEDULER
        return is_running_ ? 1 : 0;
#else
        return threads_.size();
#endif
    }
    void reset_statistics();

private:
    TaskId next_id()
    {
        return task_id_counter_.fetch_add(1, std::memory_order_relaxed);
    }
    std::shared_ptr<TaskHandle> create_handle(TaskType type, bool repeat, int interval_ms, const Group &group);
    void schedule_once(std::shared_ptr<TaskHandle> handle, OnceTask task);
    void schedule_periodic(std::shared_ptr<TaskHandle> handle, PeriodicTask task);
    void cancel_internal(TaskId task_id);
    bool suspend_internal(TaskId task_id);
    bool resume_internal(TaskId task_id);
    bool wait_tasks_internal(const std::vector<TaskId> &task_ids, int timeout_ms);
    void remove_task_internal(TaskId task_id, const Group &group);
    bool post_internal(OnceTask task, TaskId *id, const Group &group, bool enable_immediate);
    void mark_finished(std::shared_ptr<TaskHandle> handle, bool success);
    void invoke_pre_execute_callback(TaskId task_id, TaskType task_type, const Group &group);
    void invoke_post_execute_callback(TaskId task_id, TaskType task_type, bool success, const Group &group);

    std::atomic<bool> is_running_ = false;
#if !BROOKESIA_LIB_UTILS_USE_WASM_SINGLE_THREAD_SCHEDULER
    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> io_work_guard_;
    boost::thread_group threads_;
#endif
    std::map<TaskId, std::shared_ptr<TaskHandle>> tasks_;
    std::map<Group, std::unordered_set<TaskId>> groups_;
#if !BROOKESIA_LIB_UTILS_USE_WASM_SINGLE_THREAD_SCHEDULER
    std::map<Group, std::shared_ptr<boost::asio::strand<boost::asio::io_context::executor_type>>> strands_;
#endif
    mutable boost::mutex mutex_;
    std::atomic<TaskId> task_id_counter_ = 1;
    std::atomic<TaskId> total_tasks_ = 0;
    std::atomic<TaskId> completed_tasks_ = 0;
    std::atomic<TaskId> failed_tasks_ = 0;
    std::atomic<TaskId> canceled_tasks_ = 0;
    std::atomic<TaskId> suspended_tasks_ = 0;
    std::atomic<size_t> worker_wait_slot_count_ = 0;
    std::map<Group, PreExecuteCallback> pre_execute_callbacks_;
    std::map<Group, PostExecuteCallback> post_execute_callbacks_;
};

} // namespace esp_brookesia::lib_utils
