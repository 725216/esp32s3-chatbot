/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include "brookesia/hal_interface/interfaces/bluetooth/ble/peripheral.hpp"

namespace esp_brookesia::hal {

class BluezPeripheralBackend {
public:
    BluezPeripheralBackend();
    ~BluezPeripheralBackend();

    BluezPeripheralBackend(const BluezPeripheralBackend &) = delete;
    BluezPeripheralBackend &operator=(const BluezPeripheralBackend &) = delete;
    BluezPeripheralBackend(BluezPeripheralBackend &&) = delete;
    BluezPeripheralBackend &operator=(BluezPeripheralBackend &&) = delete;

    bool configure(const bluetooth::ble::PeripheralConfig &config, bluetooth::ble::PeripheralIface::Callbacks callbacks);
    bool clear_callbacks();
    bool init(std::string *error_message = nullptr);
    bool deinit();
    bool start();
    bool stop();
    bool start_advertising();
    bool stop_advertising();
    std::vector<bluetooth::ble::ConnectionInfo> get_connections() const;
    bool is_subscribed(uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic) const;
    bool notify(
        uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic, const bluetooth::ble::ByteArray &data
    );
    bool disconnect(uint16_t connection_id);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace esp_brookesia::hal
