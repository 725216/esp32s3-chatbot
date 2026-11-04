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

namespace esp_brookesia::hal::bluetooth::ble {

/**
 * @brief Deterministic in-memory BLE Peripheral backend for host and WASM simulation.
 *
 * This class deliberately performs no radio I/O. Platform HALs expose explicit test hooks
 * for injecting the remote-central side of the simulated connection.
 */
class SimulatedPeripheralBackend {
public:
    SimulatedPeripheralBackend();
    ~SimulatedPeripheralBackend();

    SimulatedPeripheralBackend(const SimulatedPeripheralBackend &) = delete;
    SimulatedPeripheralBackend &operator=(const SimulatedPeripheralBackend &) = delete;
    SimulatedPeripheralBackend(SimulatedPeripheralBackend &&) = delete;
    SimulatedPeripheralBackend &operator=(SimulatedPeripheralBackend &&) = delete;

    bool configure(const PeripheralConfig &config, PeripheralIface::Callbacks callbacks);
    bool clear_callbacks();
    bool init();
    bool deinit();
    bool start();
    bool stop();
    bool start_advertising();
    bool stop_advertising();
    std::vector<ConnectionInfo> get_connections() const;
    bool is_subscribed(uint16_t connection_id, const CharacteristicId &characteristic) const;
    bool notify(uint16_t connection_id, const CharacteristicId &characteristic, const ByteArray &data);
    bool disconnect(uint16_t connection_id);

    bool simulate_connect(uint16_t connection_id, std::string peer_address);
    bool simulate_disconnect(uint16_t connection_id, std::string reason, bool restart_advertising = true);
    bool simulate_mtu_change(uint16_t connection_id, uint16_t mtu);
    bool simulate_subscription(
        uint16_t connection_id, const CharacteristicId &characteristic, bool subscribed
    );
    bool simulate_write(uint16_t connection_id, const CharacteristicId &characteristic, const ByteArray &data);
    std::vector<WriteEvent> take_notifications();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace esp_brookesia::hal::bluetooth::ble
