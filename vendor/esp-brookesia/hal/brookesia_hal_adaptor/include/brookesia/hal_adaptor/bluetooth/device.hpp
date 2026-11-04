/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "brookesia/hal_interface/device.hpp"

namespace esp_brookesia::hal {

/** ESP Bluetooth device registration point shared by BLE and Classic profiles. */
class BluetoothDevice final: public Device {
public:
    static constexpr const char *DEVICE_NAME = "Bluetooth";
    static constexpr const char *A2DP_IFACE_NAME = "Bluetooth:A2dpSink";

    static BluetoothDevice &get_instance()
    {
        static BluetoothDevice instance;
        return instance;
    }

protected:
    BluetoothDevice();
    bool probe() override;
    std::vector<InterfaceSpec> get_interface_specs() const override;
    bool on_init() override;
    void on_deinit() override;
};

} // namespace esp_brookesia::hal
