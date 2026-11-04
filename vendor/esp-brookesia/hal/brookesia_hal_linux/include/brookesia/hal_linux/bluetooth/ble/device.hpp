/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include "brookesia/hal_interface/device.hpp"
#include "brookesia/hal_interface/interfaces/bluetooth/ble/types.hpp"

namespace esp_brookesia::hal {

class BleLinuxBackend;

class BleLinuxDevice: public Device {
public:
    static constexpr const char *DEVICE_NAME = "BleLinux";
    static constexpr const char *PERIPHERAL_IFACE_NAME = "BleLinux:Peripheral";

    BleLinuxDevice(const BleLinuxDevice &) = delete;
    BleLinuxDevice &operator=(const BleLinuxDevice &) = delete;
    BleLinuxDevice(BleLinuxDevice &&) = delete;
    BleLinuxDevice &operator=(BleLinuxDevice &&) = delete;

    static BleLinuxDevice &get_instance()
    {
        static BleLinuxDevice instance;
        return instance;
    }

private:
    BleLinuxDevice()
        : Device(std::string(DEVICE_NAME))
    {
    }
    ~BleLinuxDevice() = default;

    bool probe() override;
    std::vector<InterfaceSpec> get_interface_specs() const override;
    bool on_init() override;
    void on_deinit() override;

    std::shared_ptr<BleLinuxBackend> backend_;
};

namespace bluetooth::ble::linux_test {

bool simulate_connect(uint16_t connection_id, std::string peer_address = "00:00:00:00:00:01");
bool simulate_disconnect(uint16_t connection_id, std::string reason = "test_disconnect");
bool simulate_mtu_change(uint16_t connection_id, uint16_t mtu);
bool simulate_subscription(
    uint16_t connection_id, const CharacteristicId &characteristic, bool subscribed
);
bool simulate_write(uint16_t connection_id, const CharacteristicId &characteristic, const ByteArray &data);
std::vector<WriteEvent> take_notifications();

} // namespace bluetooth::ble::linux_test

} // namespace esp_brookesia::hal
