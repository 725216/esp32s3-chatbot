/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file provider.hpp
 * @brief Provider contract for DataFlow adapters.
 */

#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "brookesia/service_manager/dataflow/topology.hpp"

namespace esp_brookesia::service::dataflow {

struct AudioCaptureOperationConfig;
struct AudioPlaybackOperationConfig;
struct VisualOperationConfig;
class AudioCaptureOperation;
class AudioPlaybackOperation;
class VisualOperation;

class DataFlowProvider {
public:
    virtual ~DataFlowProvider() = default;

    virtual ProviderInfo get_provider_info() const = 0;
    virtual std::vector<OutputInfo> list_outputs(Model model) const = 0;
    virtual std::expected<std::shared_ptr<VisualOperation>, std::string> open_visual_operation(
        const VisualOperationConfig &config
    );
    virtual std::expected<std::shared_ptr<AudioPlaybackOperation>, std::string> open_audio_playback_operation(
        const AudioPlaybackOperationConfig &config
    );
    virtual std::expected<std::shared_ptr<AudioCaptureOperation>, std::string> open_audio_capture_operation(
        const AudioCaptureOperationConfig &config
    );
};

} // namespace esp_brookesia::service::dataflow
