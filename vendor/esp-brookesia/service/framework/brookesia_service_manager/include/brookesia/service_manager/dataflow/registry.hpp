/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file registry.hpp
 * @brief Manager-owned DataFlow provider discovery and operation registry.
 */

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "brookesia/lib_utils/signal.hpp"
#include "brookesia/service_manager/dataflow/audio/capture_operation.hpp"
#include "brookesia/service_manager/dataflow/audio/playback_operation.hpp"
#include "brookesia/service_manager/dataflow/provider.hpp"
#include "brookesia/service_manager/dataflow/registration.hpp"
#include "brookesia/service_manager/dataflow/visual/operation.hpp"

namespace esp_brookesia::service {

class ServiceManager;

namespace dataflow {

/**
 * @brief Manager-owned registry for provider discovery and bound operations.
 */
class DataFlowRegistry {
public:
    explicit DataFlowRegistry(ServiceManager &manager);
    ~DataFlowRegistry();

    DataFlowRegistry(const DataFlowRegistry &) = delete;
    DataFlowRegistry &operator=(const DataFlowRegistry &) = delete;

    std::expected<ProviderRegistration, std::string> register_provider(std::shared_ptr<DataFlowProvider> provider);
    void unregister_provider(std::string_view provider_id);
    /**
     * @brief Invalidate operations owned by a stopping provider without removing
     * its discoverable registration.
     *
     * Providers call this from `on_stop()` so the registry can still select and
     * bind the provider again on a later open operation.
     */
    void invalidate_provider_operations(std::string_view provider_id);
    std::vector<ProviderInfo> list_providers() const;
    std::vector<OutputInfo> list_outputs(Model model, std::string_view provider_id = {}) const;

    using ProviderChangedCallback = std::function<void(const ProviderInfo &info, bool registered)>;
    using OutputChangedCallback = std::function<void(const OutputInfo &info, bool registered)>;
    using OperationStateChangedCallback = std::function<void(const OperationInfo &info)>;
    using ActiveSourceChangedCallback = std::function<void(
                                            const OperationInfo &info, const std::string &output_name,
                                            const std::string &source_name
                                        )>;

    lib_utils::connection connect_provider_changed(ProviderChangedCallback callback);
    lib_utils::connection connect_output_changed(OutputChangedCallback callback);
    lib_utils::connection connect_operation_state_changed(OperationStateChangedCallback callback);
    lib_utils::connection connect_active_source_changed(ActiveSourceChangedCallback callback);

    std::expected<std::shared_ptr<VisualOperation>, std::string> open_visual_operation(
        VisualOperationConfig config
    );
    std::expected<std::shared_ptr<AudioPlaybackOperation>, std::string> open_audio_playback_operation(
        AudioPlaybackOperationConfig config
    );
    std::expected<std::shared_ptr<AudioCaptureOperation>, std::string> open_audio_capture_operation(
        AudioCaptureOperationConfig config
    );
    std::shared_ptr<DataFlowOperation> get_operation(std::string_view operation_id) const;
    std::vector<std::shared_ptr<DataFlowOperation>> list_operations(std::string_view owner = {}) const;
    void release_operation(std::string_view operation_id);
    void release_operations_for_owner(std::string_view owner);

private:
    void remove_operation_record(std::string_view operation_id);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dataflow
} // namespace esp_brookesia::service
