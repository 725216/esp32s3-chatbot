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

class BleWasmBackend;

class BleWasmDevice: public Device {
public:
    static constexpr const char *DEVICE_NAME = "BleWasm";
    static constexpr const char *PERIPHERAL_IFACE_NAME = "BleWasm:Peripheral";

    BleWasmDevice(const BleWasmDevice &) = delete;
    BleWasmDevice &operator=(const BleWasmDevice &) = delete;
    BleWasmDevice(BleWasmDevice &&) = delete;
    BleWasmDevice &operator=(BleWasmDevice &&) = delete;

    static BleWasmDevice &get_instance()
    {
        static BleWasmDevice instance;
        return instance;
    }

private:
    BleWasmDevice()
        : Device(std::string(DEVICE_NAME))
    {
    }
    ~BleWasmDevice() = default;

    bool probe() override;
    std::vector<InterfaceSpec> get_interface_specs() const override;
    bool on_init() override;
    void on_deinit() override;

    std::shared_ptr<BleWasmBackend> backend_;
};

namespace bluetooth::ble::wasm_test {

bool simulate_connect(uint16_t connection_id, std::string peer_address = "00:00:00:00:00:01");
bool simulate_disconnect(uint16_t connection_id, std::string reason = "test_disconnect");
bool simulate_mtu_change(uint16_t connection_id, uint16_t mtu);
bool simulate_subscription(
    uint16_t connection_id, const CharacteristicId &characteristic, bool subscribed
);
bool simulate_write(uint16_t connection_id, const CharacteristicId &characteristic, const ByteArray &data);
std::vector<WriteEvent> take_notifications();

} // namespace bluetooth::ble::wasm_test

} // namespace esp_brookesia::hal
