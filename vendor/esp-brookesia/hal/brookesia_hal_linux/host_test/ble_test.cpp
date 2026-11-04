/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "brookesia/hal_interface.hpp"
#include "brookesia/hal_linux.hpp"

namespace {

using namespace esp_brookesia;

constexpr const char *SERVICE_UUID_1 = "7A5A0001-6C8D-4F5A-9C2E-3B9E0B2F4A10";
constexpr const char *SERVICE_UUID_2 = "7A5A0011-6C8D-4F5A-9C2E-3B9E0B2F4A10";
constexpr const char *SHARED_CHARACTERISTIC_UUID = "7A5A0002-6C8D-4F5A-9C2E-3B9E0B2F4A10";
constexpr const char *NOTIFY_CHARACTERISTIC_UUID = "7A5A0003-6C8D-4F5A-9C2E-3B9E0B2F4A10";

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "[FAILED] " << message << '\n';
    }
    return condition;
}

hal::bluetooth::ble::PeripheralConfig make_config()
{
    return {
        .device_name = "Brookesia-BLE-Test",
        .preferred_mtu = 247,
        .max_connections = 1,
        .auto_restart_advertising = true,
        .advertised_service_uuids = {SERVICE_UUID_1},
        .services = {
            {
                .uuid = SERVICE_UUID_1,
                .characteristics = {
                    {
                        .uuid = SHARED_CHARACTERISTIC_UUID,
                        .write = true,
                    },
                    {
                        .uuid = NOTIFY_CHARACTERISTIC_UUID,
                        .notify = true,
                    },
                },
            },
            {
                .uuid = SERVICE_UUID_2,
                .characteristics = {
                    {
                        .uuid = SHARED_CHARACTERISTIC_UUID,
                        .write_without_response = true,
                    },
                },
            },
        },
    };
}

bool test_validation()
{
    bool result = true;
    result &= expect(hal::bluetooth::ble::is_valid_uuid(SERVICE_UUID_1), "canonical uppercase UUID is accepted");
    result &= expect(
                  hal::bluetooth::ble::normalize_uuid(SERVICE_UUID_1) == "7a5a0001-6c8d-4f5a-9c2e-3b9e0b2f4a10",
                  "UUID normalization is lowercase"
              );
    result &= expect(!hal::bluetooth::ble::is_valid_uuid("7a5a0001"), "short UUID is rejected");

    std::string error;
    auto config = make_config();
    result &= expect(
                  hal::bluetooth::ble::validate_peripheral_config(config, &error),
                  "same characteristic UUID is valid in different services"
              );

    auto invalid = config;
    invalid.device_name.clear();
    result &= expect(!hal::bluetooth::ble::validate_peripheral_config(invalid), "empty device name is rejected");
    invalid = config;
    invalid.preferred_mtu = 22;
    result &= expect(!hal::bluetooth::ble::validate_peripheral_config(invalid), "MTU below 23 is rejected");
    invalid.preferred_mtu = 528;
    result &= expect(!hal::bluetooth::ble::validate_peripheral_config(invalid), "MTU above 527 is rejected");
    invalid = config;
    invalid.max_connections = 2;
    result &= expect(!hal::bluetooth::ble::validate_peripheral_config(invalid), "multiple connections are rejected");
    invalid = config;
    invalid.services.push_back(invalid.services.front());
    result &= expect(!hal::bluetooth::ble::validate_peripheral_config(invalid), "duplicate service UUID is rejected");
    invalid = config;
    invalid.services.front().characteristics.push_back(invalid.services.front().characteristics.front());
    result &= expect(
                  !hal::bluetooth::ble::validate_peripheral_config(invalid),
                  "duplicate characteristic UUID in one service is rejected"
              );
    invalid = config;
    invalid.services.clear();
    result &= expect(!hal::bluetooth::ble::validate_peripheral_config(invalid), "empty service list is rejected");
    return result;
}

bool test_lifecycle_and_data_path()
{
    bool result = true;
    auto peripheral = hal::acquire_first_interface<hal::bluetooth::ble::PeripheralIface>();
    result &= expect(peripheral != nullptr, "BLE Peripheral interface is discoverable");
    if (!peripheral) {
        return false;
    }

    int advertising_started = 0;
    int advertising_stopped = 0;
    int connected = 0;
    int disconnected = 0;
    int subscribed = 0;
    int unsubscribed = 0;
    int mtu_changed = 0;
    int errors = 0;
    hal::bluetooth::ble::WriteEvent last_write;
    hal::bluetooth::ble::PeripheralIface::Callbacks callbacks{
        .on_advertising_state_changed = [&](bool advertising)
        {
            advertising ? ++advertising_started : ++advertising_stopped;
        },
        .on_connection_state_changed = [&](const auto &, bool is_connected, const auto &)
        {
            is_connected ? ++connected : ++disconnected;
        },
        .on_mtu_changed = [&](uint16_t, uint16_t) { ++mtu_changed; },
        .on_subscription_changed = [&](uint16_t, const auto &, bool is_subscribed)
        {
            is_subscribed ? ++subscribed : ++unsubscribed;
        },
        .on_characteristic_written = [&](const auto & event) { last_write = event; },
        .on_error = [&](const auto &, int, const auto &) { ++errors; },
    };

    const auto config = make_config();
    const hal::bluetooth::ble::CharacteristicId write_id{
        .service_uuid = SERVICE_UUID_1,
        .characteristic_uuid = SHARED_CHARACTERISTIC_UUID,
    };
    const hal::bluetooth::ble::CharacteristicId notify_id{
        .service_uuid = SERVICE_UUID_1,
        .characteristic_uuid = NOTIFY_CHARACTERISTIC_UUID,
    };
    const hal::bluetooth::ble::ByteArray payload{0x00, 0x7F, 0x80, 0xFF};

    result &= expect(peripheral->configure(config, callbacks), "configure succeeds");
    result &= expect(peripheral->init() && peripheral->init(), "init is idempotent");
    result &= expect(peripheral->start() && peripheral->start(), "start is idempotent");
    result &= expect(
                  peripheral->start_advertising() && peripheral->start_advertising(),
                  "start advertising is idempotent"
              );
    result &= expect(advertising_started == 1, "advertising callback fires once per transition");

    result &= expect(hal::bluetooth::ble::linux_test::simulate_connect(7, "AA:BB:CC:DD:EE:FF"), "connection injects");
    result &= expect(connected == 1 && advertising_stopped == 1, "connect callbacks are emitted");
    result &= expect(peripheral->get_connections().size() == 1, "connection snapshot is populated");
    result &= expect(hal::bluetooth::ble::linux_test::simulate_mtu_change(7, 247), "MTU update injects");
    result &= expect(mtu_changed == 1, "MTU callback is emitted");
    result &= expect(
                  hal::bluetooth::ble::linux_test::simulate_subscription(7, notify_id, true),
                  "notification subscription injects"
              );
    result &= expect(subscribed == 1 && peripheral->is_subscribed(7, notify_id), "subscription is queryable");
    result &= expect(hal::bluetooth::ble::linux_test::simulate_write(7, write_id, payload), "binary write injects");
    result &= expect(
                  last_write.connection_id == 7 && last_write.characteristic ==
    hal::bluetooth::ble::CharacteristicId{
        .service_uuid = "7a5a0001-6c8d-4f5a-9c2e-3b9e0b2f4a10",
        .characteristic_uuid = "7a5a0002-6c8d-4f5a-9c2e-3b9e0b2f4a10",
    } && last_write.data == payload,
    "write callback owns normalized binary data"
              );

    result &= expect(peripheral->notify(7, notify_id, payload), "subscribed notification succeeds");
    const auto notifications = hal::bluetooth::ble::linux_test::take_notifications();
    result &= expect(
                  notifications.size() == 1 && notifications.front().connection_id == 7 &&
                  notifications.front().characteristic.characteristic_uuid ==
                  "7a5a0003-6c8d-4f5a-9c2e-3b9e0b2f4a10" && notifications.front().data == payload,
                  "notification capture preserves target and bytes"
              );
    const hal::bluetooth::ble::ByteArray oversized_payload(245, 0x5A);
    result &= expect(
                  !hal::bluetooth::ble::linux_test::simulate_write(7, write_id, oversized_payload),
                  "write larger than MTU-3 is rejected"
              );
    result &= expect(
                  !peripheral->notify(7, notify_id, oversized_payload),
                  "notification larger than MTU-3 is rejected"
              );
    result &= expect(
                  hal::bluetooth::ble::linux_test::simulate_subscription(7, notify_id, false),
                  "unsubscribe injects"
              );
    result &= expect(unsubscribed == 1 && !peripheral->is_subscribed(7, notify_id), "unsubscribe is queryable");
    result &= expect(!peripheral->notify(7, notify_id, payload), "notification without subscription fails");
    result &= expect(errors == 3, "rejected ATT operations report diagnostic errors");

    result &= expect(peripheral->disconnect(7), "disconnect succeeds");
    result &= expect(disconnected == 1 && advertising_started == 2, "disconnect automatically restarts advertising");
    result &= expect(peripheral->get_connections().empty(), "disconnect clears connection snapshot");
    result &= expect(peripheral->stop() && peripheral->stop(), "stop is idempotent");
    result &= expect(peripheral->deinit() && peripheral->deinit(), "deinit is idempotent");
    result &= expect(peripheral->clear_callbacks(), "callbacks clear successfully");
    return result;
}

} // namespace

int main()
{
    const bool passed = test_validation() && test_lifecycle_and_data_path();
    if (passed) {
        std::cout << "BLE HAL validation passed\n";
        return 0;
    }
    return 1;
}
