/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "brookesia/hal_interface/interfaces/bluetooth/ble/peripheral.hpp"
#include "brookesia/hal_interface/interfaces/bluetooth/ble/simulated_peripheral.hpp"
#include "brookesia/hal_linux/macro_configs.h"
#if !BROOKESIA_HAL_LINUX_BLE_DEVICE_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "brookesia/hal_linux.hpp"
#include "bluez_backend.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::hal {

class BleLinuxBackend {
public:
    bool configure(const bluetooth::ble::PeripheralConfig &config, bluetooth::ble::PeripheralIface::Callbacks callbacks)
    {
        std::string error_message;
        if (!bluetooth::ble::validate_peripheral_config(config, &error_message)) {
            report_error(callbacks, "configure", -1, error_message);
            return false;
        }

        bool busy = false;
        {
            std::lock_guard lock(mutex_);
            busy = static_cast<bool>(bluez_) || static_cast<bool>(simulated_);
            if (!busy) {
                config_ = bluetooth::ble::normalize_peripheral_config(config);
                callbacks_ = callbacks;
                configured_ = true;
            }
        }
        if (busy) {
            report_error(callbacks, "configure", -2, "deinit BLE before replacing its configuration");
        }
        return !busy;
    }

    bool clear_callbacks()
    {
        std::shared_ptr<BluezPeripheralBackend> bluez;
        std::shared_ptr<bluetooth::ble::SimulatedPeripheralBackend> simulated;
        {
            std::lock_guard lock(mutex_);
            callbacks_ = {};
            bluez = bluez_;
            simulated = simulated_;
        }
        if (bluez) {
            bluez->clear_callbacks();
        }
        if (simulated) {
            simulated->clear_callbacks();
        }
        return true;
    }

    bool init()
    {
        bluetooth::ble::PeripheralConfig config;
        bluetooth::ble::PeripheralIface::Callbacks callbacks;
        bool configured = false;
        {
            std::lock_guard lock(mutex_);
            if (bluez_ || simulated_) {
                return true;
            }
            configured = configured_;
            if (!configured) {
                callbacks = callbacks_;
            } else {
                config = config_;
                callbacks = callbacks_;
            }
        }
        if (!configured) {
            report_error(callbacks, "init", -1, "BLE peripheral is not configured");
            return false;
        }

#if BROOKESIA_HAL_LINUX_BLE_BACKEND_BLUEZ_AVAILABLE
        auto bluez = std::make_shared<BluezPeripheralBackend>();
        std::string error_message;
        if (bluez->configure(config, callbacks) && bluez->init(&error_message)) {
            std::lock_guard lock(mutex_);
            bluez_ = std::move(bluez);
            BROOKESIA_LOGI("Using BlueZ BLE Peripheral backend");
            return true;
        }
#if BROOKESIA_HAL_LINUX_BLE_BACKEND_STRICT
        report_error(callbacks, "init", -2, error_message.empty() ? "BlueZ initialization failed" : error_message);
        return false;
#else
        BROOKESIA_LOGW("BlueZ BLE backend unavailable (%1%); falling back to deterministic stub", error_message);
#endif
#endif

        auto simulated = std::make_shared<bluetooth::ble::SimulatedPeripheralBackend>();
        if (!simulated->configure(config, callbacks) || !simulated->init()) {
            return false;
        }
        std::lock_guard lock(mutex_);
        simulated_ = std::move(simulated);
        BROOKESIA_LOGI("Using deterministic BLE Peripheral stub backend");
        return true;
    }

    bool deinit()
    {
        std::shared_ptr<BluezPeripheralBackend> bluez;
        std::shared_ptr<bluetooth::ble::SimulatedPeripheralBackend> simulated;
        {
            std::lock_guard lock(mutex_);
            bluez = std::move(bluez_);
            simulated = std::move(simulated_);
        }
        bool result = true;
        if (bluez) {
            result = bluez->deinit() && result;
        }
        if (simulated) {
            result = simulated->deinit() && result;
        }
        return result;
    }

    bool start()
    {
        return dispatch("start", &BluezPeripheralBackend::start, &bluetooth::ble::SimulatedPeripheralBackend::start);
    }
    bool stop()
    {
        return dispatch("stop", &BluezPeripheralBackend::stop, &bluetooth::ble::SimulatedPeripheralBackend::stop);
    }
    bool start_advertising()
    {
        return dispatch(
                   "start_advertising", &BluezPeripheralBackend::start_advertising,
                   &bluetooth::ble::SimulatedPeripheralBackend::start_advertising
               );
    }
    bool stop_advertising()
    {
        return dispatch(
                   "stop_advertising", &BluezPeripheralBackend::stop_advertising,
                   &bluetooth::ble::SimulatedPeripheralBackend::stop_advertising
               );
    }

    std::vector<bluetooth::ble::ConnectionInfo> get_connections() const
    {
        std::shared_ptr<BluezPeripheralBackend> bluez;
        std::shared_ptr<bluetooth::ble::SimulatedPeripheralBackend> simulated;
        {
            std::lock_guard lock(mutex_);
            bluez = bluez_;
            simulated = simulated_;
        }
        if (bluez) {
            return bluez->get_connections();
        }
        return simulated ? simulated->get_connections() : std::vector<bluetooth::ble::ConnectionInfo> {};
    }

    bool is_subscribed(uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic) const
    {
        std::shared_ptr<BluezPeripheralBackend> bluez;
        std::shared_ptr<bluetooth::ble::SimulatedPeripheralBackend> simulated;
        {
            std::lock_guard lock(mutex_);
            bluez = bluez_;
            simulated = simulated_;
        }
        if (bluez) {
            return bluez->is_subscribed(connection_id, characteristic);
        }
        return simulated && simulated->is_subscribed(connection_id, characteristic);
    }

    bool notify(
        uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic, const bluetooth::ble::ByteArray &data
    )
    {
        std::shared_ptr<BluezPeripheralBackend> bluez;
        std::shared_ptr<bluetooth::ble::SimulatedPeripheralBackend> simulated;
        {
            std::lock_guard lock(mutex_);
            bluez = bluez_;
            simulated = simulated_;
        }
        if (bluez) {
            return bluez->notify(connection_id, characteristic, data);
        }
        return simulated && simulated->notify(connection_id, characteristic, data);
    }

    bool disconnect(uint16_t connection_id)
    {
        std::shared_ptr<BluezPeripheralBackend> bluez;
        std::shared_ptr<bluetooth::ble::SimulatedPeripheralBackend> simulated;
        {
            std::lock_guard lock(mutex_);
            bluez = bluez_;
            simulated = simulated_;
        }
        if (bluez) {
            return bluez->disconnect(connection_id);
        }
        return simulated && simulated->disconnect(connection_id);
    }

    std::shared_ptr<bluetooth::ble::SimulatedPeripheralBackend> get_simulated_backend() const
    {
        std::lock_guard lock(mutex_);
        return simulated_;
    }

private:
    using BluezAction = bool (BluezPeripheralBackend::*)();
    using SimulatedAction = bool (bluetooth::ble::SimulatedPeripheralBackend::*)();

    bool dispatch(const char *operation, BluezAction bluez_action, SimulatedAction simulated_action)
    {
        std::shared_ptr<BluezPeripheralBackend> bluez;
        std::shared_ptr<bluetooth::ble::SimulatedPeripheralBackend> simulated;
        bluetooth::ble::PeripheralIface::Callbacks callbacks;
        {
            std::lock_guard lock(mutex_);
            bluez = bluez_;
            simulated = simulated_;
            callbacks = callbacks_;
        }
        if (bluez) {
            return ((*bluez).*bluez_action)();
        }
        if (simulated) {
            return ((*simulated).*simulated_action)();
        }
        report_error(callbacks, operation, -1, "BLE peripheral is not initialized");
        return false;
    }

    static void report_error(
        const bluetooth::ble::PeripheralIface::Callbacks &callbacks, const std::string &operation, int code,
        const std::string &message
    )
    {
        if (callbacks.on_error) {
            callbacks.on_error(operation, code, message);
        }
    }

    mutable std::mutex mutex_;
    bluetooth::ble::PeripheralConfig config_;
    bluetooth::ble::PeripheralIface::Callbacks callbacks_;
    std::shared_ptr<BluezPeripheralBackend> bluez_;
    std::shared_ptr<bluetooth::ble::SimulatedPeripheralBackend> simulated_;
    bool configured_ = false;
};

namespace {

std::mutex active_backend_mutex;
std::weak_ptr<BleLinuxBackend> active_backend;

class BleLinuxPeripheralIface: public bluetooth::ble::PeripheralIface {
public:
    explicit BleLinuxPeripheralIface(std::shared_ptr<BleLinuxBackend> backend)
        : backend_(std::move(backend))
    {
    }

    bool configure(const bluetooth::ble::PeripheralConfig &config, Callbacks callbacks) override
    {
        return backend_->configure(config, std::move(callbacks));
    }
    bool clear_callbacks() override
    {
        return backend_->clear_callbacks();
    }
    bool init() override
    {
        return backend_->init();
    }
    bool deinit() override
    {
        return backend_->deinit();
    }
    bool start() override
    {
        return backend_->start();
    }
    bool stop() override
    {
        return backend_->stop();
    }
    bool start_advertising() override
    {
        return backend_->start_advertising();
    }
    bool stop_advertising() override
    {
        return backend_->stop_advertising();
    }
    std::vector<bluetooth::ble::ConnectionInfo> get_connections() const override
    {
        return backend_->get_connections();
    }
    bool is_subscribed(uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic) const override
    {
        return backend_->is_subscribed(connection_id, characteristic);
    }
    bool notify(
        uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic, const bluetooth::ble::ByteArray &data
    ) override
    {
        return backend_->notify(connection_id, characteristic, data);
    }
    bool disconnect(uint16_t connection_id) override
    {
        return backend_->disconnect(connection_id);
    }

private:
    std::shared_ptr<BleLinuxBackend> backend_;
};

std::shared_ptr<bluetooth::ble::SimulatedPeripheralBackend> get_simulated_backend()
{
    std::lock_guard lock(active_backend_mutex);
    const auto backend = active_backend.lock();
    return backend ? backend->get_simulated_backend() : nullptr;
}

} // namespace

bool BleLinuxDevice::probe()
{
    return true;
}

std::vector<InterfaceSpec> BleLinuxDevice::get_interface_specs() const
{
    return {{bluetooth::ble::PeripheralIface::NAME, PERIPHERAL_IFACE_NAME}};
}

bool BleLinuxDevice::on_init()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    backend_ = std::make_shared<BleLinuxBackend>();
    interfaces_.emplace(PERIPHERAL_IFACE_NAME, std::make_shared<BleLinuxPeripheralIface>(backend_));
    {
        std::lock_guard lock(active_backend_mutex);
        active_backend = backend_;
    }
    return true;
}

void BleLinuxDevice::on_deinit()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    interfaces_.erase(PERIPHERAL_IFACE_NAME);
    if (backend_ != nullptr) {
        backend_->deinit();
        backend_->clear_callbacks();
    }
    {
        std::lock_guard lock(active_backend_mutex);
        active_backend.reset();
    }
    backend_.reset();
}

namespace bluetooth::ble::linux_test {

bool simulate_connect(uint16_t connection_id, std::string peer_address)
{
    const auto backend = get_simulated_backend();
    return backend && backend->simulate_connect(connection_id, std::move(peer_address));
}

bool simulate_disconnect(uint16_t connection_id, std::string reason)
{
    const auto backend = get_simulated_backend();
    return backend && backend->simulate_disconnect(connection_id, std::move(reason));
}

bool simulate_mtu_change(uint16_t connection_id, uint16_t mtu)
{
    const auto backend = get_simulated_backend();
    return backend && backend->simulate_mtu_change(connection_id, mtu);
}

bool simulate_subscription(
    uint16_t connection_id, const CharacteristicId &characteristic, bool subscribed
)
{
    const auto backend = get_simulated_backend();
    return backend && backend->simulate_subscription(connection_id, characteristic, subscribed);
}

bool simulate_write(uint16_t connection_id, const CharacteristicId &characteristic, const ByteArray &data)
{
    const auto backend = get_simulated_backend();
    return backend && backend->simulate_write(connection_id, characteristic, data);
}

std::vector<WriteEvent> take_notifications()
{
    const auto backend = get_simulated_backend();
    return backend ? backend->take_notifications() : std::vector<WriteEvent> {};
}

} // namespace bluetooth::ble::linux_test

#if BROOKESIA_HAL_LINUX_ENABLE_BLE_DEVICE
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    Device, BleLinuxDevice, BleLinuxDevice::DEVICE_NAME, BleLinuxDevice::get_instance(),
    BROOKESIA_HAL_LINUX_BLE_DEVICE_PLUGIN_SYMBOL
);
#endif

} // namespace esp_brookesia::hal
