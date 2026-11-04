/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include "brookesia/hal_interface/device.hpp"

namespace esp_brookesia::hal {

class BleEspBackend;

class BleDevice: public Device {
public:
    static constexpr const char *DEVICE_NAME = "BLE";
    static constexpr const char *PERIPHERAL_IFACE_NAME = "BLE:Peripheral";

    BleDevice(const BleDevice &) = delete;
    BleDevice &operator=(const BleDevice &) = delete;
    BleDevice(BleDevice &&) = delete;
    BleDevice &operator=(BleDevice &&) = delete;

    static BleDevice &get_instance()
    {
        static BleDevice instance;
        return instance;
    }

private:
    BleDevice()
        : Device(std::string(DEVICE_NAME))
    {
    }
    ~BleDevice() = default;

    bool probe() override;
    std::vector<InterfaceSpec> get_interface_specs() const override;
    bool on_init() override;
    void on_deinit() override;

    std::shared_ptr<BleEspBackend> backend_;
};

} // namespace esp_brookesia::hal
