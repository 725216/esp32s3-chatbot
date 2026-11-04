/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file playback_operation.hpp
 * @brief Provider-backed native audio playback operation API.
 */

#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "brookesia/service_manager/dataflow/audio/types.hpp"
#include "brookesia/service_manager/dataflow/operation.hpp"

namespace esp_brookesia::service::dataflow {

struct AudioPlaybackOperationConfig : OperationConfig {
    AudioStreamConfig stream;
    bool open_stream = false;
};

class AudioPlaybackOperation : public DataFlowOperation {
public:
    using ReleaseCallback = std::function<void(AudioWriteResult result)>;

    virtual std::vector<OutputInfo> get_outputs() const = 0;
    virtual std::expected<void, std::string> request_output(std::string_view output_name) = 0;
    virtual std::expected<void, std::string> release_output(std::string_view output_name) = 0;
    virtual std::expected<void, std::string> set_active_source(std::string_view output_name) = 0;
    virtual std::expected<std::string, std::string> get_active_source(std::string_view output_name) const = 0;
    virtual std::expected<void, std::string> open_stream(
        std::string_view output_name, const AudioStreamConfig &config
    ) = 0;
    virtual std::expected<void, std::string> close_stream(std::string_view output_name) = 0;
    virtual AudioWriteResult write_copy(
        std::string_view output_name, std::span<const uint8_t> data, uint32_t timeout_ms
    ) = 0;
    /**
     * @brief Queue borrowed PCM/encoded bytes.
     *
     * `on_release` is invoked exactly once on success, cancellation, or error;
     * callers must retain `data` until it runs.
     */
    virtual AudioWriteResult write_borrowed(
        std::string_view output_name, std::span<const uint8_t> data, ReleaseCallback on_release,
        uint32_t timeout_ms
    ) = 0;
    virtual bool is_stream_drained(std::string_view output_name) const = 0;
};

} // namespace esp_brookesia::service::dataflow
