/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <functional>
#include <string>
#include <vector>
#include "brookesia/hal_interface/interface.hpp"
#include "brookesia/hal_interface/interfaces/bluetooth/ble/types.hpp"

/**
 * @file peripheral.hpp
 * @brief Declares the BLE Peripheral/GATT Server HAL interface.
 */

namespace esp_brookesia::hal::bluetooth::ble {

/**
 * @brief Single-connection BLE Peripheral and dynamic GATT Server interface.
 */
class PeripheralIface: public Interface {
public:
    static constexpr const char *NAME = "BlePeripheral";

    /** @brief Event callbacks emitted by the platform backend. */
    struct Callbacks {
        std::function<void(bool)> on_advertising_state_changed; ///< Advertising state changed.
        /// Connection snapshot, connected state, and disconnect reason.
        std::function<void(const ConnectionInfo &, bool, const std::string &)> on_connection_state_changed;
        std::function<void(uint16_t, uint16_t)> on_mtu_changed; ///< Connection ID and negotiated ATT MTU.
        std::function<void(uint16_t, const CharacteristicId &, bool)> on_subscription_changed; ///< CCCD state.
        std::function<void(const WriteEvent &)> on_characteristic_written; ///< Owned remote write payload.
        std::function<void(const std::string &, int, const std::string &)> on_error; ///< Operation, code, message.
    };

    PeripheralIface()
        : Interface(NAME)
    {
    }

    virtual ~PeripheralIface() = default;

    /** @brief Set the dynamic GATT configuration and callbacks while idle. */
    virtual bool configure(const PeripheralConfig &config, Callbacks callbacks) = 0;
    /** @brief Remove all callbacks without changing the current lifecycle state. */
    virtual bool clear_callbacks() = 0;
    /** @brief Initialize the platform BLE host and configured GATT database. */
    virtual bool init() = 0;
    /** @brief Release the platform BLE host resources. */
    virtual bool deinit() = 0;
    /** @brief Start the configured peripheral without automatically advertising. */
    virtual bool start() = 0;
    /** @brief Stop advertising and active connections. */
    virtual bool stop() = 0;
    /** @brief Publish the configured connectable advertisement. */
    virtual bool start_advertising() = 0;
    /** @brief Stop advertising without disconnecting an active peer. */
    virtual bool stop_advertising() = 0;
    /** @brief Return a snapshot of active connections. */
    virtual std::vector<ConnectionInfo> get_connections() const = 0;
    /** @brief Test whether a connection subscribed to a notify characteristic. */
    virtual bool is_subscribed(uint16_t connection_id, const CharacteristicId &characteristic) const = 0;
    /** @brief Send one notification, subject to subscription and negotiated MTU checks. */
    virtual bool notify(
        uint16_t connection_id, const CharacteristicId &characteristic, const ByteArray &data
    ) = 0;
    /** @brief Disconnect one active peer. */
    virtual bool disconnect(uint16_t connection_id) = 0;
};

} // namespace esp_brookesia::hal::bluetooth::ble
