/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <memory>
#include <utility>
#include "brookesia/hal_interface/device.hpp"
#include "brookesia/hal_interface/interfaces/bluetooth/simulated_a2dp_sink.hpp"
#include "brookesia/hal_wasm.hpp"
#include "brookesia/lib_utils/plugin.hpp"

namespace esp_brookesia::hal {

namespace {
std::shared_ptr<bluetooth::SimulatedA2dpSinkBackend> a2dp_backend;
}

BtWasmDevice::BtWasmDevice()
    : Device(DEVICE_NAME)
{
}

bool BtWasmDevice::probe()
{
    return true;
}

std::vector<InterfaceSpec> BtWasmDevice::get_interface_specs() const
{
    return {{bluetooth::A2dpSinkIface::NAME, A2DP_IFACE_NAME}};
}

bool BtWasmDevice::on_init()
{
    a2dp_backend = std::make_shared<bluetooth::SimulatedA2dpSinkBackend>();
    a2dp_backend->set_supported(true);
    interfaces_.emplace(A2DP_IFACE_NAME, a2dp_backend);
    return true;
}

void BtWasmDevice::on_deinit()
{
    interfaces_.clear();
    a2dp_backend.reset();
}

BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    Device, BtWasmDevice, BtWasmDevice::DEVICE_NAME, BtWasmDevice::get_instance(),
    bt_wasm_device_symbol
);

namespace bluetooth::wasm_test {

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

} // namespace bluetooth::wasm_test

} // namespace esp_brookesia::hal
