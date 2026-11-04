/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "brookesia/hal_interface/interfaces/bluetooth/simulated_a2dp_sink.hpp"

namespace esp_brookesia::hal {

class BtLinuxDevice final: public Device {
public:
    static constexpr const char *DEVICE_NAME = "BluetoothLinux";
    static constexpr const char *A2DP_IFACE_NAME = "Bluetooth:A2dpSink";

    static BtLinuxDevice &get_instance()
    {
        static BtLinuxDevice instance;
        return instance;
    }

protected:
    BtLinuxDevice();
    bool probe() override;
    std::vector<InterfaceSpec> get_interface_specs() const override;
    bool on_init() override;
    void on_deinit() override;
};

/** Deterministic A2DP injection hooks used by Linux host tests and simulator tools. */
namespace bluetooth::linux_test {
bool set_a2dp_supported(bool supported);
bool simulate_a2dp_connect(hal::bluetooth::PeerInfo peer);
bool simulate_a2dp_disconnect();
bool simulate_a2dp_stream(hal::bluetooth::StreamState state, hal::bluetooth::PcmFrame frame = {});
bool simulate_a2dp_playback_status(hal::bluetooth::PlaybackStatus status);
bool simulate_a2dp_metadata(hal::bluetooth::TrackMetadata metadata);
bool simulate_a2dp_volume(uint8_t volume);
} // namespace bluetooth::linux_test

} // namespace esp_brookesia::hal
