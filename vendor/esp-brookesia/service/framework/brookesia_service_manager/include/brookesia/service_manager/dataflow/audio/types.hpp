/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file types.hpp
 * @brief Audio format, stream, capture, and result descriptors.
 */

#include <cstdint>
#include <optional>

#include "brookesia/lib_utils/describe_helpers.hpp"

namespace esp_brookesia::service::dataflow {

enum class AudioCodecFormat : uint8_t {
    PCM,
    OPUS,
    G711A,
    Unknown,
};

enum class AudioStreamQueuePolicy : uint8_t {
    DropNewest,
};

struct AudioGeneralConfig {
    uint8_t channels = 0;
    uint8_t sample_bits = 0;
    uint32_t sample_rate = 0;
    uint8_t frame_duration = 0;
};

struct AudioStreamConfig {
    AudioCodecFormat type = AudioCodecFormat::PCM;
    AudioGeneralConfig general;
    uint32_t queue_size_bytes = 32 * 1024;
    AudioStreamQueuePolicy queue_policy = AudioStreamQueuePolicy::DropNewest;
};

struct AudioEncoderExtraConfigOpus {
    bool enable_vbr = false;
    uint32_t bitrate = 0;
};

enum class AudioWriteResult : uint8_t {
    Written,
    DroppedNotActive,
    DroppedQueueFull,
    DroppedInvalidData,
    Closed,
    Error,
};

enum class AudioAfeEvent : uint8_t {
    VadStart,
    VadEnd,
    WakeStart,
    WakeEnd,
    Unknown,
};

struct AudioCaptureConfig {
    AudioCodecFormat type = AudioCodecFormat::PCM;
    AudioGeneralConfig general;
    std::optional<AudioEncoderExtraConfigOpus> opus = std::nullopt;
    uint32_t fetch_interval_ms = 10;
    uint32_t fetch_data_size = 4096;
    bool enable_afe = false;
    uint32_t afe_wake_start_timeout_ms = 30000;
    uint32_t afe_wake_end_timeout_ms = 10000;
};

} // namespace esp_brookesia::service::dataflow

namespace esp_brookesia::service::dataflow {

BROOKESIA_DESCRIBE_ENUM(esp_brookesia::service::dataflow::AudioCodecFormat, PCM, OPUS, G711A, Unknown);
BROOKESIA_DESCRIBE_ENUM(esp_brookesia::service::dataflow::AudioStreamQueuePolicy, DropNewest);
BROOKESIA_DESCRIBE_ENUM(
    esp_brookesia::service::dataflow::AudioWriteResult,
    Written,
    DroppedNotActive,
    DroppedQueueFull,
    DroppedInvalidData,
    Closed,
    Error
);
BROOKESIA_DESCRIBE_ENUM(
    esp_brookesia::service::dataflow::AudioAfeEvent, VadStart, VadEnd, WakeStart, WakeEnd, Unknown
);
BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::AudioGeneralConfig, (), (channels, sample_bits, sample_rate, frame_duration)
);
BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::AudioStreamConfig, (), (type, general, queue_size_bytes, queue_policy)
);
BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::AudioEncoderExtraConfigOpus, (), (enable_vbr, bitrate)
);
BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::AudioCaptureConfig,
    (),
    (type, general, opus, fetch_interval_ms, fetch_data_size, enable_afe, afe_wake_start_timeout_ms,
     afe_wake_end_timeout_ms)
);

} // namespace esp_brookesia::service::dataflow
