/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file capture_operation.hpp
 * @brief Provider-backed native audio capture operation API.
 */

#include <expected>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "brookesia/lib_utils/signal.hpp"
#include "brookesia/service_manager/dataflow/audio/types.hpp"
#include "brookesia/service_manager/dataflow/operation.hpp"

namespace esp_brookesia::service::dataflow {

struct AudioCaptureOperationConfig : OperationConfig {
    AudioCaptureConfig capture;
};

class AudioCaptureOperation : public DataFlowOperation {
public:
    using DataCallback = std::function<void(std::span<const uint8_t> data)>;
    using AfeEventCallback = std::function<void(AudioAfeEvent event)>;

    virtual std::expected<void, std::string> start(const AudioCaptureConfig &config) = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void pause_wake_end() = 0;
    virtual void resume_wake_end() = 0;
    virtual bool is_started() const = 0;
    virtual bool is_paused() const = 0;
    virtual std::vector<std::string> get_afe_wake_words() const = 0;
    virtual lib_utils::connection connect_data(DataCallback callback) = 0;
    virtual lib_utils::connection connect_afe_event(AfeEventCallback callback) = 0;
};

} // namespace esp_brookesia::service::dataflow
