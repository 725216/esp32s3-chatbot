/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <utility>

#include "private/task_scheduler_impl.hpp"

namespace esp_brookesia::lib_utils {

TaskScheduler::TaskScheduler():
    impl_(std::make_unique<Impl>())
{
}

TaskScheduler::~TaskScheduler() = default;

bool TaskScheduler::start(const StartConfig &config)
{
    return impl_->start(config);
}

bool TaskScheduler::start()
{
    return start(StartConfig{});
}

void TaskScheduler::stop()
{
    impl_->stop();
}

bool TaskScheduler::is_running() const
{
    return impl_->is_running();
}

bool TaskScheduler::is_current_thread_worker() const
{
    return impl_->is_current_thread_worker();
}

bool TaskScheduler::is_current_thread_in_group(const Group &group) const
{
    return impl_->is_current_thread_in_group(group);
}

bool TaskScheduler::try_acquire_worker_wait_slot()
{
    return impl_->try_acquire_worker_wait_slot();
}

void TaskScheduler::release_worker_wait_slot()
{
    impl_->release_worker_wait_slot();
}

bool TaskScheduler::configure_group(const Group &group, const GroupConfig &config)
{
    return impl_->configure_group(group, config);
}

bool TaskScheduler::dispatch(OnceTask task, TaskId *id, const Group &group)
{
    return impl_->dispatch(std::move(task), id, group);
}

bool TaskScheduler::post(OnceTask task, TaskId *id, const Group &group)
{
    return impl_->post(std::move(task), id, group);
}

bool TaskScheduler::post_delayed(OnceTask task, int delay_ms, TaskId *id, const Group &group)
{
    return impl_->post_delayed(std::move(task), delay_ms, id, group);
}

bool TaskScheduler::post_periodic(PeriodicTask task, int interval_ms, TaskId *id, const Group &group)
{
    return impl_->post_periodic(std::move(task), interval_ms, id, group);
}

bool TaskScheduler::post_batch(std::vector<OnceTask> tasks, std::vector<TaskId> *ids, const Group &group)
{
    return impl_->post_batch(std::move(tasks), ids, group);
}

void TaskScheduler::cancel(TaskId id)
{
    impl_->cancel(id);
}

void TaskScheduler::cancel_group(const Group &group)
{
    impl_->cancel_group(group);
}

void TaskScheduler::cancel_all()
{
    impl_->cancel_all();
}

bool TaskScheduler::suspend(TaskId id)
{
    return impl_->suspend(id);
}

size_t TaskScheduler::suspend_group(const Group &group)
{
    return impl_->suspend_group(group);
}

size_t TaskScheduler::suspend_all()
{
    return impl_->suspend_all();
}

bool TaskScheduler::resume(TaskId id)
{
    return impl_->resume(id);
}

size_t TaskScheduler::resume_group(const Group &group)
{
    return impl_->resume_group(group);
}

size_t TaskScheduler::resume_all()
{
    return impl_->resume_all();
}

bool TaskScheduler::wait(TaskId id, int timeout_ms)
{
    return impl_->wait(id, timeout_ms);
}

bool TaskScheduler::wait_group(const Group &group, int timeout_ms)
{
    return impl_->wait_group(group, timeout_ms);
}

bool TaskScheduler::wait_all(int timeout_ms)
{
    return impl_->wait_all(timeout_ms);
}

bool TaskScheduler::restart_timer(TaskId id)
{
    return impl_->restart_timer(id);
}

TaskScheduler::TaskType TaskScheduler::get_type(TaskId id) const
{
    return impl_->get_type(id);
}

TaskScheduler::TaskState TaskScheduler::get_state(TaskId id) const
{
    return impl_->get_state(id);
}

TaskScheduler::Group TaskScheduler::get_group(TaskId id) const
{
    return impl_->get_group(id);
}

size_t TaskScheduler::get_group_task_count(const Group &group) const
{
    return impl_->get_group_task_count(group);
}

std::vector<TaskScheduler::Group> TaskScheduler::get_active_groups() const
{
    return impl_->get_active_groups();
}

TaskScheduler::Statistics TaskScheduler::get_statistics() const
{
    return impl_->get_statistics();
}

size_t TaskScheduler::get_worker_count() const
{
    return impl_->get_worker_count();
}

void TaskScheduler::reset_statistics()
{
    impl_->reset_statistics();
}

} // namespace esp_brookesia::lib_utils
