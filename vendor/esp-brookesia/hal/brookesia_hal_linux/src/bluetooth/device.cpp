/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <memory>
#include <utility>
#include "brookesia/hal_interface/device.hpp"
#include "brookesia/hal_interface/interfaces/bluetooth/simulated_a2dp_sink.hpp"
#include "brookesia/hal_linux.hpp"
#include "brookesia/hal_linux/macro_configs.h"
#include "brookesia/lib_utils/plugin.hpp"

namespace esp_brookesia::hal {

namespace {
std::shared_ptr<bluetooth::SimulatedA2dpSinkBackend> a2dp_backend;
}

BtLinuxDevice::BtLinuxDevice()
    : Device(DEVICE_NAME)
{
}

bool BtLinuxDevice::probe()
{
    return true;
}

std::vector<InterfaceSpec> BtLinuxDevice::get_interface_specs() const
{
    return {{bluetooth::A2dpSinkIface::NAME, A2DP_IFACE_NAME}};
}

bool BtLinuxDevice::on_init()
{
    a2dp_backend = std::make_shared<bluetooth::SimulatedA2dpSinkBackend>();
    // Linux exposes a deterministic in-memory A2DP endpoint.  It deliberately
    // does not claim to be a BlueZ audio device; tests inject peer/PCM events.
    a2dp_backend->set_supported(true);
    interfaces_.emplace(A2DP_IFACE_NAME, a2dp_backend);
    return true;
}

void BtLinuxDevice::on_deinit()
{
    interfaces_.clear();
    a2dp_backend.reset();
}

#if BROOKESIA_HAL_LINUX_ENABLE_BT_DEVICE
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    Device, BtLinuxDevice, BtLinuxDevice::DEVICE_NAME, BtLinuxDevice::get_instance(),
    BROOKESIA_HAL_LINUX_BT_DEVICE_PLUGIN_SYMBOL
);
#endif

namespace bluetooth::linux_test {

bool set_a2dp_supported(bool supported)
{
    return a2dp_backend != nullptr ? (a2dp_backend->set_supported(supported), true) : false;
}

bool simulate_a2dp_connect(hal::bluetooth::PeerInfo peer)
{
    return a2dp_backend != nullptr && a2dp_backend->simulate_connect(std::move(peer));
}

bool simulate_a2dp_disconnect()
{
    return a2dp_backend != nullptr && a2dp_backend->simulate_disconnect();
}

bool simulate_a2dp_stream(hal::bluetooth::StreamState state, hal::bluetooth::PcmFrame frame)
{
    return a2dp_backend != nullptr && a2dp_backend->simulate_stream(state, std::move(frame));
}

bool simulate_a2dp_playback_status(hal::bluetooth::PlaybackStatus status)
{
    return a2dp_backend != nullptr && a2dp_backend->simulate_playback_status(status);
}

bool simulate_a2dp_metadata(hal::bluetooth::TrackMetadata metadata)
{
    return a2dp_backend != nullptr && a2dp_backend->simulate_metadata(std::move(metadata));
}

bool simulate_a2dp_volume(uint8_t volume)
{
    return a2dp_backend != nullptr && a2dp_backend->simulate_volume(volume);
}

} // namespace bluetooth::linux_test

} // namespace esp_brookesia::hal
