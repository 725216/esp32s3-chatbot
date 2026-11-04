/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file types.hpp
 * @brief Defines common Bluetooth host and profile value types.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "brookesia/lib_utils/describe_helpers.hpp"

namespace esp_brookesia::hal::bluetooth {

/** Bluetooth profile exposed by the host. */
enum class Profile {
    BleGattPeripheral,
    A2dpSink,
    AvrcpController,
    Max,
};

/** Shared Bluetooth host lifecycle state. */
enum class HostState {
    Idle,
    Starting,
    Started,
    Stopping,
    Error,
    Max,
};

/** Connection state reported by a Bluetooth profile. */
enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Disconnecting,
    Max,
};

/** A2DP media stream state. */
enum class StreamState {
    Idle,
    Starting,
    Started,
    Stopping,
    Stopped,
    Error,
    Max,
};

/** AVRCP playback status. */
enum class PlaybackStatus {
    Unknown,
    Stopped,
    Playing,
    Paused,
    Max,
};

/** PCM format delivered by an A2DP sink backend after decoding. */
struct PcmFormat {
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    uint8_t bits = 0;

    bool operator==(const PcmFormat &) const = default;
};

/** Bluetooth device configuration shared by profile backends. */
struct DeviceConfig {
    std::string device_name;
    bool discoverable = true;
    bool connectable = true;

    bool operator==(const DeviceConfig &) const = default;
};

/** Connected peer information. */
struct PeerInfo {
    uint16_t connection_id = 0;
    std::string address;
    std::string name;

    bool operator==(const PeerInfo &) const = default;
};

/** Track metadata reported by AVRCP. */
struct TrackMetadata {
    std::string title;
    std::string artist;
    std::string album;
    uint32_t duration_ms = 0;

    bool operator==(const TrackMetadata &) const = default;
};

/** Owned PCM frame delivered to native C++ consumers. */
struct PcmFrame {
    std::vector<uint8_t> data;
    PcmFormat format;
    uint64_t timestamp_ms = 0;
};

BROOKESIA_DESCRIBE_ENUM(
    Profile,
    BleGattPeripheral, A2dpSink, AvrcpController, Max
);
BROOKESIA_DESCRIBE_ENUM(
    HostState,
    Idle, Starting, Started, Stopping, Error, Max
);
BROOKESIA_DESCRIBE_ENUM(
    ConnectionState,
    Disconnected, Connecting, Connected, Disconnecting, Max
);
BROOKESIA_DESCRIBE_ENUM(
    StreamState,
    Idle, Starting, Started, Stopping, Stopped, Error, Max
);
BROOKESIA_DESCRIBE_ENUM(
    PlaybackStatus,
    Unknown, Stopped, Playing, Paused, Max
);
BROOKESIA_DESCRIBE_STRUCT(PcmFormat, (), (sample_rate, channels, bits));
BROOKESIA_DESCRIBE_STRUCT(DeviceConfig, (), (device_name, discoverable, connectable));
BROOKESIA_DESCRIBE_STRUCT(PeerInfo, (), (connection_id, address, name));
BROOKESIA_DESCRIBE_STRUCT(TrackMetadata, (), (title, artist, album, duration_ms));

} // namespace esp_brookesia::hal::bluetooth
