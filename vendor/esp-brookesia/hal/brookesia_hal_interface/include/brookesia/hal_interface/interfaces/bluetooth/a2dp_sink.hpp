/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file a2dp_sink.hpp
 * @brief Declares the Classic Bluetooth A2DP Sink HAL interface.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "brookesia/hal_interface/interface.hpp"
#include "brookesia/hal_interface/interfaces/bluetooth/types.hpp"

namespace esp_brookesia::hal::bluetooth {

/** Classic Bluetooth A2DP Sink profile interface. */
class A2dpSinkIface: public Interface {
public:
    static constexpr const char *NAME = "BluetoothA2dpSink";

    struct Config {
        DeviceConfig device;
        PcmFormat output_format;
    };

    struct Callbacks {
        std::function<void(const PeerInfo &, ConnectionState)> on_connection_changed;
        std::function<void(StreamState)> on_stream_changed;
        std::function<void(PlaybackStatus)> on_playback_status_changed;
        std::function<void(const TrackMetadata &)> on_metadata_changed;
        std::function<void(uint8_t)> on_volume_changed;
        std::function<void(PcmFrame)> on_pcm;
        std::function<void(std::string)> on_error;
    };

    A2dpSinkIface()
        : Interface(NAME)
    {
    }

    ~A2dpSinkIface() override = default;

    virtual bool is_supported() const = 0;
    virtual bool configure(const Config &config, Callbacks callbacks) = 0;
    virtual void clear_callbacks() = 0;
    virtual bool init() = 0;
    virtual void deinit() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual bool next() = 0;
    virtual bool previous() = 0;
    virtual bool set_volume(uint8_t volume) = 0;
    virtual uint8_t get_volume() const = 0;
    virtual std::optional<PeerInfo> get_connection() const = 0;
    virtual bool disconnect() = 0;
};

BROOKESIA_DESCRIBE_STRUCT(A2dpSinkIface::Config, (), (device, output_format));

} // namespace esp_brookesia::hal::bluetooth
