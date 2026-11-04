/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <utility>
#include "brookesia/hal_interface/interfaces/bluetooth/ble/simulated_peripheral.hpp"

namespace esp_brookesia::hal::bluetooth::ble {

namespace {

struct SubscriptionKey {
    uint16_t connection_id = 0;
    CharacteristicId characteristic;

    bool operator<(const SubscriptionKey &other) const
    {
        return std::tie(connection_id, characteristic.service_uuid, characteristic.characteristic_uuid) <
               std::tie(
                   other.connection_id, other.characteristic.service_uuid, other.characteristic.characteristic_uuid
               );
    }
};

CharacteristicId normalize_characteristic(const CharacteristicId &characteristic)
{
    return {
        .service_uuid = normalize_uuid(characteristic.service_uuid),
        .characteristic_uuid = normalize_uuid(characteristic.characteristic_uuid),
    };
}

const CharacteristicConfig *find_characteristic(const PeripheralConfig &config, const CharacteristicId &id)
{
    const auto normalized = normalize_characteristic(id);
    for (const auto &service : config.services) {
        if (service.uuid != normalized.service_uuid) {
            continue;
        }
        const auto result = std::find_if(
                                service.characteristics.begin(), service.characteristics.end(),
        [&normalized](const auto & characteristic) {
            return characteristic.uuid == normalized.characteristic_uuid;
        }
                            );
        return result == service.characteristics.end() ? nullptr : &*result;
    }
    return nullptr;
}

void report_error(
    const PeripheralIface::Callbacks &callbacks, const std::string &operation, int code,
    const std::string &message
)
{
    if (callbacks.on_error) {
        callbacks.on_error(operation, code, message);
    }
}

} // namespace

class SimulatedPeripheralBackend::Impl {
public:
    mutable std::mutex mutex;
    PeripheralConfig config;
    PeripheralIface::Callbacks callbacks;
    std::map<uint16_t, ConnectionInfo> connections;
    std::set<SubscriptionKey> subscriptions;
    std::vector<WriteEvent> notifications;
    bool configured = false;
    bool initialized = false;
    bool started = false;
    bool advertising = false;
};

SimulatedPeripheralBackend::SimulatedPeripheralBackend()
    : impl_(std::make_unique<Impl>())
{
}

SimulatedPeripheralBackend::~SimulatedPeripheralBackend() = default;

bool SimulatedPeripheralBackend::configure(const PeripheralConfig &config, PeripheralIface::Callbacks callbacks)
{
    std::string error_message;
    if (!validate_peripheral_config(config, &error_message)) {
        report_error(callbacks, "configure", -1, error_message);
        return false;
    }

    bool available = false;
    {
        std::lock_guard lock(impl_->mutex);
        available = !impl_->initialized && !impl_->started && !impl_->advertising && impl_->connections.empty();
        if (available) {
            impl_->config = normalize_peripheral_config(config);
            impl_->callbacks = callbacks;
            impl_->configured = true;
        }
    }
    if (!available) {
        report_error(callbacks, "configure", -2, "BLE peripheral must be idle before configure");
    }
    return available;
}

bool SimulatedPeripheralBackend::clear_callbacks()
{
    std::lock_guard lock(impl_->mutex);
    impl_->callbacks = {};
    return true;
}

bool SimulatedPeripheralBackend::init()
{
    PeripheralIface::Callbacks callbacks;
    bool configured = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->initialized) {
            return true;
        }
        configured = impl_->configured;
        callbacks = impl_->callbacks;
        if (configured) {
            impl_->initialized = true;
        }
    }
    if (!configured) {
        report_error(callbacks, "init", -1, "BLE peripheral is not configured");
    }
    return configured;
}

bool SimulatedPeripheralBackend::deinit()
{
    if (!stop()) {
        return false;
    }
    std::lock_guard lock(impl_->mutex);
    impl_->initialized = false;
    return true;
}

bool SimulatedPeripheralBackend::start()
{
    PeripheralIface::Callbacks callbacks;
    bool initialized = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->started) {
            return true;
        }
        initialized = impl_->initialized;
        callbacks = impl_->callbacks;
        if (initialized) {
            impl_->started = true;
        }
    }
    if (!initialized) {
        report_error(callbacks, "start", -1, "BLE peripheral is not initialized");
    }
    return initialized;
}

bool SimulatedPeripheralBackend::stop()
{
    stop_advertising();

    std::vector<uint16_t> connection_ids;
    {
        std::lock_guard lock(impl_->mutex);
        for (const auto &[connection_id, unused] : impl_->connections) {
            (void)unused;
            connection_ids.push_back(connection_id);
        }
    }
    for (const auto connection_id : connection_ids) {
        simulate_disconnect(connection_id, "peripheral_stopped", false);
    }

    std::lock_guard lock(impl_->mutex);
    impl_->started = false;
    return true;
}

bool SimulatedPeripheralBackend::start_advertising()
{
    PeripheralIface::Callbacks callbacks;
    std::string error_message;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->advertising) {
            return true;
        }
        callbacks = impl_->callbacks;
        if (!impl_->started) {
            error_message = "BLE peripheral is not started";
        } else if (!impl_->connections.empty()) {
            error_message = "single BLE connection is already active";
        } else {
            impl_->advertising = true;
        }
    }
    if (!error_message.empty()) {
        report_error(callbacks, "start_advertising", -1, error_message);
        return false;
    }
    if (callbacks.on_advertising_state_changed) {
        callbacks.on_advertising_state_changed(true);
    }
    return true;
}

bool SimulatedPeripheralBackend::stop_advertising()
{
    PeripheralIface::Callbacks callbacks;
    bool changed = false;
    {
        std::lock_guard lock(impl_->mutex);
        changed = impl_->advertising;
        if (changed) {
            impl_->advertising = false;
            callbacks = impl_->callbacks;
        }
    }
    if (changed && callbacks.on_advertising_state_changed) {
        callbacks.on_advertising_state_changed(false);
    }
    return true;
}

std::vector<ConnectionInfo> SimulatedPeripheralBackend::get_connections() const
{
    std::lock_guard lock(impl_->mutex);
    std::vector<ConnectionInfo> result;
    result.reserve(impl_->connections.size());
    for (const auto &[unused, connection] : impl_->connections) {
        (void)unused;
        result.push_back(connection);
    }
    return result;
}

bool SimulatedPeripheralBackend::is_subscribed(
    uint16_t connection_id, const CharacteristicId &characteristic
) const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->subscriptions.contains({connection_id, normalize_characteristic(characteristic)});
}

bool SimulatedPeripheralBackend::notify(
    uint16_t connection_id, const CharacteristicId &characteristic, const ByteArray &data
)
{
    PeripheralIface::Callbacks callbacks;
    std::string error_message;
    {
        std::lock_guard lock(impl_->mutex);
        const auto connection = impl_->connections.find(connection_id);
        const auto normalized = normalize_characteristic(characteristic);
        const auto *characteristic_config = find_characteristic(impl_->config, normalized);
        if (connection == impl_->connections.end()) {
            error_message = "connection does not exist";
        } else if ((characteristic_config == nullptr) || !characteristic_config->notify) {
            error_message = "characteristic is not notify-capable";
        } else if (!impl_->subscriptions.contains({connection_id, normalized})) {
            error_message = "client is not subscribed to the characteristic";
        } else if (data.size() > static_cast<size_t>(connection->second.mtu - 3U)) {
            error_message = "notification exceeds negotiated ATT MTU";
        } else {
            impl_->notifications.push_back({connection_id, normalized, data});
            return true;
        }
        callbacks = impl_->callbacks;
    }
    report_error(callbacks, "notify", -1, error_message);
    return false;
}

bool SimulatedPeripheralBackend::disconnect(uint16_t connection_id)
{
    return simulate_disconnect(connection_id, "local_disconnect", true);
}

bool SimulatedPeripheralBackend::simulate_connect(uint16_t connection_id, std::string peer_address)
{
    PeripheralIface::Callbacks callbacks;
    ConnectionInfo connection;
    bool accepted = false;
    {
        std::lock_guard lock(impl_->mutex);
        callbacks = impl_->callbacks;
        accepted = impl_->started && impl_->advertising && impl_->connections.empty() &&
                   !impl_->connections.contains(connection_id);
        if (accepted) {
            connection = {
                .connection_id = connection_id,
                .peer_address = std::move(peer_address),
                .mtu = ATT_MTU_MIN,
            };
            impl_->connections.emplace(connection_id, connection);
            impl_->advertising = false;
        }
    }
    if (!accepted) {
        report_error(callbacks, "simulate_connect", -1, "peripheral is not accepting a connection");
        return false;
    }
    if (callbacks.on_advertising_state_changed) {
        callbacks.on_advertising_state_changed(false);
    }
    if (callbacks.on_connection_state_changed) {
        callbacks.on_connection_state_changed(connection, true, "");
    }
    return true;
}

bool SimulatedPeripheralBackend::simulate_disconnect(
    uint16_t connection_id, std::string reason, bool restart_advertising
)
{
    PeripheralIface::Callbacks callbacks;
    ConnectionInfo connection;
    std::vector<CharacteristicId> subscriptions;
    bool found = false;
    bool should_restart = false;
    {
        std::lock_guard lock(impl_->mutex);
        callbacks = impl_->callbacks;
        const auto result = impl_->connections.find(connection_id);
        found = result != impl_->connections.end();
        if (found) {
            connection = result->second;
            impl_->connections.erase(result);
            for (auto iterator = impl_->subscriptions.begin(); iterator != impl_->subscriptions.end();) {
                if (iterator->connection_id == connection_id) {
                    subscriptions.push_back(iterator->characteristic);
                    iterator = impl_->subscriptions.erase(iterator);
                } else {
                    ++iterator;
                }
            }
            should_restart = restart_advertising && impl_->started && impl_->config.auto_restart_advertising;
        }
    }
    if (!found) {
        report_error(callbacks, "disconnect", -1, "connection does not exist");
        return false;
    }
    if (callbacks.on_subscription_changed) {
        for (const auto &characteristic : subscriptions) {
            callbacks.on_subscription_changed(connection_id, characteristic, false);
        }
    }
    if (callbacks.on_connection_state_changed) {
        callbacks.on_connection_state_changed(connection, false, reason);
    }
    return !should_restart || start_advertising();
}

bool SimulatedPeripheralBackend::simulate_mtu_change(uint16_t connection_id, uint16_t mtu)
{
    PeripheralIface::Callbacks callbacks;
    bool accepted = false;
    {
        std::lock_guard lock(impl_->mutex);
        callbacks = impl_->callbacks;
        const auto connection = impl_->connections.find(connection_id);
        accepted = (connection != impl_->connections.end()) && (mtu >= ATT_MTU_MIN) && (mtu <= ATT_MTU_MAX);
        if (accepted) {
            connection->second.mtu = mtu;
        }
    }
    if (!accepted) {
        report_error(callbacks, "simulate_mtu_change", -1, "invalid connection or ATT MTU");
        return false;
    }
    if (callbacks.on_mtu_changed) {
        callbacks.on_mtu_changed(connection_id, mtu);
    }
    return true;
}

bool SimulatedPeripheralBackend::simulate_subscription(
    uint16_t connection_id, const CharacteristicId &characteristic, bool subscribed
)
{
    PeripheralIface::Callbacks callbacks;
    const auto normalized = normalize_characteristic(characteristic);
    bool accepted = false;
    {
        std::lock_guard lock(impl_->mutex);
        callbacks = impl_->callbacks;
        const auto *config = find_characteristic(impl_->config, normalized);
        accepted = impl_->connections.contains(connection_id) && (config != nullptr) && config->notify;
        if (accepted) {
            const SubscriptionKey key{connection_id, normalized};
            if (subscribed) {
                impl_->subscriptions.insert(key);
            } else {
                impl_->subscriptions.erase(key);
            }
        }
    }
    if (!accepted) {
        report_error(callbacks, "simulate_subscription", -1, "invalid connection or notify characteristic");
        return false;
    }
    if (callbacks.on_subscription_changed) {
        callbacks.on_subscription_changed(connection_id, normalized, subscribed);
    }
    return true;
}

bool SimulatedPeripheralBackend::simulate_write(
    uint16_t connection_id, const CharacteristicId &characteristic, const ByteArray &data
)
{
    PeripheralIface::Callbacks callbacks;
    WriteEvent event;
    bool accepted = false;
    {
        std::lock_guard lock(impl_->mutex);
        callbacks = impl_->callbacks;
        const auto connection = impl_->connections.find(connection_id);
        const auto normalized = normalize_characteristic(characteristic);
        const auto *config = find_characteristic(impl_->config, normalized);
        accepted = (connection != impl_->connections.end()) && (config != nullptr) &&
                   (config->write || config->write_without_response) &&
                   (data.size() <= static_cast<size_t>(connection->second.mtu - 3U));
        if (accepted) {
            event = {connection_id, normalized, data};
        }
    }
    if (!accepted) {
        report_error(callbacks, "simulate_write", -1, "invalid write request or ATT payload length");
        return false;
    }
    if (callbacks.on_characteristic_written) {
        callbacks.on_characteristic_written(event);
    }
    return true;
}

std::vector<WriteEvent> SimulatedPeripheralBackend::take_notifications()
{
    std::lock_guard lock(impl_->mutex);
    auto notifications = std::move(impl_->notifications);
    impl_->notifications.clear();
    return notifications;
}

} // namespace esp_brookesia::hal::bluetooth::ble
