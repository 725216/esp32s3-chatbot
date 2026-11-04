/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "brookesia/lib_utils/describe_helpers.hpp"

/**
 * @file types.hpp
 * @brief Declares shared BLE peripheral HAL interface types.
 */

namespace esp_brookesia::hal::bluetooth::ble {

inline constexpr uint16_t ATT_MTU_MIN = 23;
inline constexpr uint16_t ATT_MTU_MAX = 527;
inline constexpr uint16_t ATT_MTU_DEFAULT = 247;
inline constexpr uint8_t MAX_CONNECTIONS = 1;

using ByteArray = std::vector<uint8_t>;

/**
 * @brief Uniquely identifies a characteristic within a GATT service.
 */
struct CharacteristicId {
    std::string service_uuid;
    std::string characteristic_uuid;

    bool operator==(const CharacteristicId &) const = default;
};

/**
 * @brief Runtime properties of one GATT characteristic.
 */
struct CharacteristicConfig {
    std::string uuid;
    bool write = false;
    bool write_without_response = false;
    bool notify = false;

    bool operator==(const CharacteristicConfig &) const = default;
};

/**
 * @brief Runtime definition of one primary GATT service.
 */
struct ServiceConfig {
    std::string uuid;
    std::vector<CharacteristicConfig> characteristics;

    bool operator==(const ServiceConfig &) const = default;
};

/**
 * @brief Complete single-connection BLE peripheral configuration.
 */
struct PeripheralConfig {
    std::string device_name;
    uint16_t preferred_mtu = ATT_MTU_DEFAULT;
    uint8_t max_connections = MAX_CONNECTIONS;
    bool auto_restart_advertising = true;
    std::vector<std::string> advertised_service_uuids;
    std::vector<ServiceConfig> services;

    bool operator==(const PeripheralConfig &) const = default;
};

/**
 * @brief Information about one active BLE connection.
 */
struct ConnectionInfo {
    uint16_t connection_id = 0;
    std::string peer_address;
    uint16_t mtu = ATT_MTU_MIN;

    bool operator==(const ConnectionInfo &) const = default;
};

/**
 * @brief Owned data delivered for a remote GATT characteristic write.
 */
struct WriteEvent {
    uint16_t connection_id = 0;
    CharacteristicId characteristic;
    ByteArray data;
};

/**
 * @brief Validate that a UUID uses the canonical 128-bit 8-4-4-4-12 form.
 */
bool is_valid_uuid(std::string_view uuid);

/**
 * @brief Return the lowercase canonical form of a valid UUID, or an empty string when invalid.
 */
std::string normalize_uuid(std::string_view uuid);

/**
 * @brief Validate a v1 single-connection peripheral configuration.
 *
 * @param[in] config Configuration to validate.
 * @param[out] error_message Optional diagnostic populated on failure.
 * @return `true` when the configuration is valid; otherwise `false`.
 */
bool validate_peripheral_config(const PeripheralConfig &config, std::string *error_message = nullptr);

/**
 * @brief Return a copy of a valid configuration with every UUID normalized to lowercase.
 */
PeripheralConfig normalize_peripheral_config(const PeripheralConfig &config);

BROOKESIA_DESCRIBE_STRUCT(CharacteristicId, (), (service_uuid, characteristic_uuid));
BROOKESIA_DESCRIBE_STRUCT(CharacteristicConfig, (), (uuid, write, write_without_response, notify));
BROOKESIA_DESCRIBE_STRUCT(ServiceConfig, (), (uuid, characteristics));
BROOKESIA_DESCRIBE_STRUCT(
    PeripheralConfig, (),
    (device_name, preferred_mtu, max_connections, auto_restart_advertising, advertised_service_uuids, services)
);
BROOKESIA_DESCRIBE_STRUCT(ConnectionInfo, (), (connection_id, peer_address, mtu));
BROOKESIA_DESCRIBE_STRUCT(WriteEvent, (), (connection_id, characteristic, data));

} // namespace esp_brookesia::hal::bluetooth::ble
