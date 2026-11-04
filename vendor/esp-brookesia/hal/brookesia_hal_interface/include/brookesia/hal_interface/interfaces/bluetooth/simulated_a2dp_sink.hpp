/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <mutex>
#include "brookesia/hal_interface/interfaces/bluetooth/a2dp_sink.hpp"

namespace esp_brookesia::hal::bluetooth {

/** Deterministic native test backend used by Linux and WASM Bluetooth devices. */
class SimulatedA2dpSinkBackend final: public A2dpSinkIface {
public:
    SimulatedA2dpSinkBackend() = default;
    ~SimulatedA2dpSinkBackend() override = default;

    bool is_supported() const override;
    bool configure(const Config &config, Callbacks callbacks) override;
    void clear_callbacks() override;
    bool init() override;
    void deinit() override;
    bool start() override;
    void stop() override;
    bool pause() override;
    bool resume() override;
    bool next() override;
    bool previous() override;
    bool set_volume(uint8_t volume) override;
    uint8_t get_volume() const override;
    std::optional<PeerInfo> get_connection() const override;
    bool disconnect() override;

    void set_supported(bool supported);
    bool simulate_connect(PeerInfo peer);
    bool simulate_disconnect();
    bool simulate_stream(StreamState state, PcmFrame frame = {});
    bool simulate_playback_status(PlaybackStatus status);
    bool simulate_metadata(TrackMetadata metadata);
    bool simulate_volume(uint8_t volume);

private:
    mutable std::mutex mutex_;
    Config config_;
    Callbacks callbacks_;
    std::optional<PeerInfo> connection_;
    uint8_t volume_ = 100;
    bool supported_ = false;
    bool initialized_ = false;
    bool started_ = false;
};

} // namespace esp_brookesia::hal::bluetooth
