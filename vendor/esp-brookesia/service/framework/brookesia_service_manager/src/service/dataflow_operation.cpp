/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <exception>
#include <mutex>
#include <utility>

#include "brookesia/service_manager/dataflow/audio/capture_operation.hpp"
#include "brookesia/service_manager/dataflow/audio/playback_operation.hpp"
#include "brookesia/service_manager/dataflow/operation.hpp"
#include "brookesia/service_manager/dataflow/provider.hpp"
#include "brookesia/service_manager/dataflow/visual/operation.hpp"

namespace esp_brookesia::service::dataflow {

struct DataFlowOperation::Impl {
    mutable std::mutex mutex;
    OperationInfo info;
    bool closed = false;
    std::function<void(std::string_view)> release_callback;
    std::function<void(const OperationInfo &)> state_callback;
};

DataFlowOperation::DataFlowOperation()
    : impl_(std::make_unique<Impl>())
{
}

DataFlowOperation::~DataFlowOperation() = default;

std::string DataFlowOperation::get_id() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->info.id;
}

std::string DataFlowOperation::get_owner() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->info.owner;
}

std::string DataFlowOperation::get_provider_id() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->info.provider_id;
}

Model DataFlowOperation::get_model() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->info.model;
}

OperationState DataFlowOperation::get_state() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->info.state;
}

OperationInfo DataFlowOperation::get_info() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->info;
}

bool DataFlowOperation::is_available() const
{
    const auto state = get_state();
    return (state == OperationState::Ready) || (state == OperationState::Active);
}

void DataFlowOperation::close()
{
    std::function<void(std::string_view)> release_callback;
    std::function<void(const OperationInfo &)> state_callback;
    OperationInfo info;
    bool state_changed = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->closed) {
            return;
        }
        impl_->closed = true;
        // Provider removal/restart first marks an operation Unavailable. Keep
        // that diagnostic state visible while still running the normal close
        // path that releases the held provider binding exactly once.
        if ((impl_->info.state != OperationState::Unavailable) && (impl_->info.state != OperationState::Error)) {
            impl_->info.state = OperationState::Closed;
            state_changed = true;
        }
        info = impl_->info;
        release_callback = std::move(impl_->release_callback);
        state_callback = impl_->state_callback;
    }

    try {
        on_close();
    } catch (const std::exception &) {
        // Provider cleanup must never prevent release of the manager-held binding.
    } catch (...) {
        // See the exception case above.
    }

    if (state_changed && state_callback) {
        state_callback(info);
    }
    if (release_callback) {
        release_callback(info.id);
    }
}

void DataFlowOperation::set_state(OperationState state)
{
    std::function<void(const OperationInfo &)> state_callback;
    OperationInfo info;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->closed || (impl_->info.state == state)) {
            return;
        }
        impl_->info.state = state;
        info = impl_->info;
        state_callback = impl_->state_callback;
    }

    if (state_callback) {
        state_callback(info);
    }
}

void DataFlowOperation::set_registry_metadata(
    OperationInfo info, std::function<void(std::string_view)> release_callback,
    std::function<void(const OperationInfo &)> state_callback
)
{
    std::lock_guard lock(impl_->mutex);
    if (impl_->closed) {
        return;
    }
    info.state = impl_->info.state;
    impl_->info = std::move(info);
    impl_->release_callback = std::move(release_callback);
    impl_->state_callback = std::move(state_callback);
}

std::expected<std::shared_ptr<VisualOperation>, std::string> DataFlowProvider::open_visual_operation(
    const VisualOperationConfig &config
)
{
    (void)config;
    return std::unexpected("Visual operations are not supported by this data-flow provider");
}

std::expected<std::shared_ptr<AudioPlaybackOperation>, std::string> DataFlowProvider::open_audio_playback_operation(
    const AudioPlaybackOperationConfig &config
)
{
    (void)config;
    return std::unexpected("Audio playback operations are not supported by this data-flow provider");
}

std::expected<std::shared_ptr<AudioCaptureOperation>, std::string> DataFlowProvider::open_audio_capture_operation(
    const AudioCaptureOperationConfig &config
)
{
    (void)config;
    return std::unexpected("Audio capture operations are not supported by this data-flow provider");
}

} // namespace esp_brookesia::service::dataflow
