/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file operation.hpp
 * @brief Provider-backed native visual DataFlow operation API.
 */

#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "brookesia/lib_utils/signal.hpp"
#include "brookesia/service_manager/dataflow/operation.hpp"
#include "brookesia/service_manager/dataflow/visual/types.hpp"

namespace esp_brookesia::service::dataflow {

struct VisualOperationConfig : OperationConfig {
};

class VisualOperation : public DataFlowOperation {
public:
    using BufferWriter = std::function<bool(VisualBufferView &view)>;
    using CompletionCallback = std::function<void(uint32_t frame_id, VisualPresentResult result)>;
    using SourceStateCallback = std::function<void(
                                    const std::string &source_name, const std::string &output_name, SourceState state
                                )>;
    using ActiveSourceCallback = std::function<void(const std::string &output_name, const std::string &source_name)>;

    virtual std::vector<VisualOutputInfo> get_outputs() const = 0;
    virtual std::vector<SourceInfo> get_sources() const = 0;
    virtual std::vector<std::string> get_source_roles() const = 0;
    virtual std::expected<void, std::string> request_output(std::string_view output_name) = 0;
    virtual std::expected<void, std::string> release_output(std::string_view output_name) = 0;
    virtual std::expected<void, std::string> set_active_source(std::string_view output_name) = 0;
    /**
     * @brief Select a provider-visible source by name.
     *
     * The default keeps a provider that only exposes this operation's own
     * source compatible. Visual providers with source arbitration override it
     * so a controller can restore a previously active source exactly.
     */
    virtual std::expected<void, std::string> set_active_source_named(
        std::string_view output_name, std::string_view source_name
    )
    {
        if (source_name.empty() || (source_name == get_info().source.name)) {
            return set_active_source(output_name);
        }
        return std::unexpected("This visual data-flow provider cannot select another source by name");
    }
    virtual std::expected<void, std::string> set_active_source_role(
        std::string_view output_name, std::string_view role
    ) = 0;
    virtual std::expected<std::string, std::string> get_active_source(std::string_view output_name) const = 0;
    virtual std::expected<std::string, std::string> get_active_source_role(std::string_view output_name) const = 0;
    virtual VisualPresentResult present_frame_sync(
        std::string_view output_name, const VisualFrameInfo &frame, std::span<const uint8_t> data,
        uint32_t timeout_ms
    ) = 0;
    virtual VisualPresentResult present_buffer_frame_sync(
        std::string_view output_name, const VisualFrameInfo &frame, BufferWriter writer
    ) = 0;
    virtual VisualAsyncSubmitResult present_frame_async(
        std::string_view output_name, const VisualFrameInfo &frame, std::span<const uint8_t> data,
        CompletionCallback on_complete, uint32_t timeout_ms
    ) = 0;
    virtual std::expected<VisualBufferView, std::string> map_output_buffer(std::string_view output_name) const = 0;
    virtual lib_utils::connection connect_source_state_changed(SourceStateCallback callback) = 0;
    virtual lib_utils::connection connect_active_source_changed(ActiveSourceCallback callback) = 0;
};

} // namespace esp_brookesia::service::dataflow
