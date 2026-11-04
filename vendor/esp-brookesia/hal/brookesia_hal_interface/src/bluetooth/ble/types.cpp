/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include "brookesia/hal_interface/interfaces/bluetooth/ble/types.hpp"

namespace esp_brookesia::hal::bluetooth::ble {

namespace {

constexpr size_t UUID_LENGTH = 36;
constexpr size_t UUID_SEPARATOR_0 = 8;
constexpr size_t UUID_SEPARATOR_1 = 13;
constexpr size_t UUID_SEPARATOR_2 = 18;
constexpr size_t UUID_SEPARATOR_3 = 23;

bool set_error(std::string *error_message, std::string message)
{
    if (error_message != nullptr) {
        *error_message = std::move(message);
    }
    return false;
}

} // namespace

bool is_valid_uuid(std::string_view uuid)
{
    if (uuid.size() != UUID_LENGTH) {
        return false;
    }

    for (size_t index = 0; index < uuid.size(); ++index) {
        const bool separator = (index == UUID_SEPARATOR_0) || (index == UUID_SEPARATOR_1) ||
                               (index == UUID_SEPARATOR_2) || (index == UUID_SEPARATOR_3);
        if (separator) {
            if (uuid[index] != '-') {
                return false;
            }
        } else if (std::isxdigit(static_cast<unsigned char>(uuid[index])) == 0) {
            return false;
        }
    }
    return true;
}

std::string normalize_uuid(std::string_view uuid)
{
    if (!is_valid_uuid(uuid)) {
        return {};
    }
    std::string normalized(uuid);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return normalized;
}

bool validate_peripheral_config(const PeripheralConfig &config, std::string *error_message)
{
    if (config.device_name.empty()) {
        return set_error(error_message, "device_name must not be empty");
    }
    if ((config.preferred_mtu < ATT_MTU_MIN) || (config.preferred_mtu > ATT_MTU_MAX)) {
        return set_error(error_message, "preferred_mtu must be in [23, 527]");
    }
    if (config.max_connections != MAX_CONNECTIONS) {
        return set_error(error_message, "v1 only supports max_connections=1");
    }
    if (config.services.empty()) {
        return set_error(error_message, "at least one GATT service is required");
    }

    std::set<std::string> service_uuids;
    for (const auto &service : config.services) {
        const auto service_uuid = normalize_uuid(service.uuid);
        if (service_uuid.empty()) {
            return set_error(error_message, "service UUID is not a canonical 128-bit UUID");
        }
        if (!service_uuids.emplace(service_uuid).second) {
            return set_error(error_message, "duplicate service UUID");
        }
        if (service.characteristics.empty()) {
            return set_error(error_message, "each GATT service requires at least one characteristic");
        }

        std::set<std::string> characteristic_uuids;
        for (const auto &characteristic : service.characteristics) {
            const auto characteristic_uuid = normalize_uuid(characteristic.uuid);
            if (characteristic_uuid.empty()) {
                return set_error(error_message, "characteristic UUID is not a canonical 128-bit UUID");
            }
            if (!characteristic_uuids.emplace(characteristic_uuid).second) {
                return set_error(error_message, "duplicate characteristic UUID within a service");
            }
            if (!characteristic.write && !characteristic.write_without_response && !characteristic.notify) {
                return set_error(error_message, "characteristic must enable write, write_without_response, or notify");
            }
        }
    }

    std::set<std::string> advertised_uuids;
    for (const auto &uuid : config.advertised_service_uuids) {
        const auto normalized = normalize_uuid(uuid);
        if (normalized.empty()) {
            return set_error(error_message, "advertised service UUID is not a canonical 128-bit UUID");
        }
        if (!advertised_uuids.emplace(normalized).second) {
            return set_error(error_message, "duplicate advertised service UUID");
        }
        if (!service_uuids.contains(normalized)) {
            return set_error(error_message, "advertised service UUID is not configured in services");
        }
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

PeripheralConfig normalize_peripheral_config(const PeripheralConfig &config)
{
    auto normalized = config;
    for (auto &uuid : normalized.advertised_service_uuids) {
        uuid = normalize_uuid(uuid);
    }
    for (auto &service : normalized.services) {
        service.uuid = normalize_uuid(service.uuid);
        for (auto &characteristic : service.characteristics) {
            characteristic.uuid = normalize_uuid(characteristic.uuid);
        }
    }
    return normalized;
}

} // namespace esp_brookesia::hal::bluetooth::ble
