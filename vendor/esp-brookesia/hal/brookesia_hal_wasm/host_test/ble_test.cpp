/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <iostream>
#include "brookesia/hal_interface.hpp"
#include "brookesia/hal_wasm.hpp"

namespace {

using namespace esp_brookesia;

constexpr const char *SERVICE_UUID = "7a5a0001-6c8d-4f5a-9c2e-3b9e0b2f4a10";
constexpr const char *WRITE_UUID = "7a5a0002-6c8d-4f5a-9c2e-3b9e0b2f4a10";
constexpr const char *NOTIFY_UUID = "7a5a0003-6c8d-4f5a-9c2e-3b9e0b2f4a10";

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "[FAILED] " << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    auto peripheral = hal::acquire_first_interface<hal::bluetooth::ble::PeripheralIface>();
    if (!expect(peripheral != nullptr, "WASM BLE Peripheral interface is discoverable")) {
        return 1;
    }

    int writes = 0;
    int connections = 0;
    int subscriptions = 0;
    hal::bluetooth::ble::PeripheralIface::Callbacks callbacks{
        .on_connection_state_changed = [&](const auto &, bool connected, const auto &) {
            connections += connected ? 1 : -1;
        },
        .on_subscription_changed = [&](uint16_t, const auto &, bool subscribed) {
            subscriptions += subscribed ? 1 : -1;
        },
        .on_characteristic_written = [&](const auto &) { ++writes; },
    };
    const hal::bluetooth::ble::PeripheralConfig config{
        .device_name = "Wasm-BLE-Test",
        .preferred_mtu = 247,
        .advertised_service_uuids = {SERVICE_UUID},
        .services = {
            {
                .uuid = SERVICE_UUID,
                .characteristics = {
                    {.uuid = WRITE_UUID, .write = true},
                    {.uuid = NOTIFY_UUID, .notify = true},
                },
            },
        },
    };
    const hal::bluetooth::ble::CharacteristicId write_id{SERVICE_UUID, WRITE_UUID};
    const hal::bluetooth::ble::CharacteristicId notify_id{SERVICE_UUID, NOTIFY_UUID};
    const hal::bluetooth::ble::ByteArray payload{0, 1, 127, 128, 255};

    bool result = true;
    result &= expect(peripheral->configure(config, callbacks), "WASM configure succeeds");
    result &= expect(peripheral->init() && peripheral->start(), "WASM lifecycle starts");
    result &= expect(peripheral->start_advertising(), "WASM advertising starts");
    result &= expect(hal::bluetooth::ble::wasm_test::simulate_connect(3), "WASM connection injects");
    result &= expect(hal::bluetooth::ble::wasm_test::simulate_mtu_change(3, 247), "WASM MTU injects");
    result &= expect(
                  hal::bluetooth::ble::wasm_test::simulate_subscription(3, notify_id, true),
                  "WASM subscription injects"
              );
    result &= expect(hal::bluetooth::ble::wasm_test::simulate_write(3, write_id, payload), "WASM write injects");
    result &= expect(peripheral->notify(3, notify_id, payload), "WASM notification succeeds");
    const auto notifications = hal::bluetooth::ble::wasm_test::take_notifications();
    result &= expect(
                  notifications.size() == 1 && notifications.front().data == payload,
                  "WASM notification capture preserves bytes"
              );
    result &= expect(connections == 1 && subscriptions == 1 && writes == 1, "WASM callbacks are complete");
    result &= expect(peripheral->disconnect(3), "WASM disconnect succeeds");
    result &= expect(connections == 0 && subscriptions == 0, "WASM disconnect clears simulated peer state");
    result &= expect(peripheral->stop() && peripheral->deinit(), "WASM lifecycle stops");
    result &= expect(peripheral->clear_callbacks(), "WASM callbacks clear");

    if (result) {
        std::cout << "WASM BLE HAL validation passed\n";
        return 0;
    }
    return 1;
}
