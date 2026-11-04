/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/hal_adaptor.hpp"
#include "brookesia/hal_adaptor/macro_configs.h"
#include "brookesia/hal_interface/interfaces/bluetooth/a2dp_sink.hpp"
#include "brookesia/lib_utils/plugin.hpp"
#include "a2dp_sink.hpp"

namespace esp_brookesia::hal {

namespace {

} // namespace

BluetoothDevice::BluetoothDevice()
    : Device(DEVICE_NAME)
{
}

bool BluetoothDevice::probe()
{
    return true;
}

std::vector<InterfaceSpec> BluetoothDevice::get_interface_specs() const
{
    return {{bluetooth::A2dpSinkIface::NAME, A2DP_IFACE_NAME}};
}

bool BluetoothDevice::on_init()
{
    interfaces_.emplace(A2DP_IFACE_NAME, create_a2dp_sink_iface());
    return true;
}

void BluetoothDevice::on_deinit()
{
    interfaces_.clear();
}

#if BROOKESIA_HAL_ADAPTOR_ENABLE_BT_DEVICE
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    Device, BluetoothDevice, BluetoothDevice::DEVICE_NAME, BluetoothDevice::get_instance(),
    BROOKESIA_HAL_ADAPTOR_BT_DEVICE_PLUGIN_SYMBOL
);
#endif

} // namespace esp_brookesia::hal
