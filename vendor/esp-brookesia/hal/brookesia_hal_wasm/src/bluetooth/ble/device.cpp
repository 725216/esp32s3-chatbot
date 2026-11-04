/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>
#include <mutex>
#include <utility>
#include "brookesia/hal_interface/interfaces/bluetooth/ble/peripheral.hpp"
#include "brookesia/hal_interface/interfaces/bluetooth/ble/simulated_peripheral.hpp"
#include "brookesia/hal_wasm.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::hal {

class BleWasmBackend: public bluetooth::ble::SimulatedPeripheralBackend {
};

namespace {

std::mutex active_backend_mutex;
std::weak_ptr<BleWasmBackend> active_backend;

class BleWasmPeripheralIface: public bluetooth::ble::PeripheralIface {
public:
    explicit BleWasmPeripheralIface(std::shared_ptr<BleWasmBackend> backend)
        : backend_(std::move(backend))
    {
    }

    bool configure(const bluetooth::ble::PeripheralConfig &config, Callbacks callbacks) override
    {
        return backend_->configure(config, std::move(callbacks));
    }
    bool clear_callbacks() override { return backend_->clear_callbacks(); }
    bool init() override { return backend_->init(); }
    bool deinit() override { return backend_->deinit(); }
    bool start() override { return backend_->start(); }
    bool stop() override { return backend_->stop(); }
    bool start_advertising() override { return backend_->start_advertising(); }
    bool stop_advertising() override { return backend_->stop_advertising(); }
    std::vector<bluetooth::ble::ConnectionInfo> get_connections() const override { return backend_->get_connections(); }
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
    bool disconnect(uint16_t connection_id) override { return backend_->disconnect(connection_id); }

private:
    std::shared_ptr<BleWasmBackend> backend_;
};

std::shared_ptr<BleWasmBackend> get_active_backend()
{
    std::lock_guard lock(active_backend_mutex);
    return active_backend.lock();
}

} // namespace

bool BleWasmDevice::probe()
{
    return true;
}

std::vector<InterfaceSpec> BleWasmDevice::get_interface_specs() const
{
    return {{bluetooth::ble::PeripheralIface::NAME, PERIPHERAL_IFACE_NAME}};
}

bool BleWasmDevice::on_init()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    backend_ = std::make_shared<BleWasmBackend>();
    interfaces_.emplace(PERIPHERAL_IFACE_NAME, std::make_shared<BleWasmPeripheralIface>(backend_));
    {
        std::lock_guard lock(active_backend_mutex);
        active_backend = backend_;
    }
    return true;
}

void BleWasmDevice::on_deinit()
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

namespace bluetooth::ble::wasm_test {

bool simulate_connect(uint16_t connection_id, std::string peer_address)
{
    const auto backend = get_active_backend();
    return backend && backend->simulate_connect(connection_id, std::move(peer_address));
}

bool simulate_disconnect(uint16_t connection_id, std::string reason)
{
    const auto backend = get_active_backend();
    return backend && backend->simulate_disconnect(connection_id, std::move(reason));
}

bool simulate_mtu_change(uint16_t connection_id, uint16_t mtu)
{
    const auto backend = get_active_backend();
    return backend && backend->simulate_mtu_change(connection_id, mtu);
}

bool simulate_subscription(
    uint16_t connection_id, const CharacteristicId &characteristic, bool subscribed
)
{
    const auto backend = get_active_backend();
    return backend && backend->simulate_subscription(connection_id, characteristic, subscribed);
}

bool simulate_write(uint16_t connection_id, const CharacteristicId &characteristic, const ByteArray &data)
{
    const auto backend = get_active_backend();
    return backend && backend->simulate_write(connection_id, characteristic, data);
}

std::vector<WriteEvent> take_notifications()
{
    const auto backend = get_active_backend();
    return backend ? backend->take_notifications() : std::vector<WriteEvent>{};
}

} // namespace bluetooth::ble::wasm_test

#if BROOKESIA_HAL_WASM_ENABLE_BLE_DEVICE
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    Device, BleWasmDevice, BleWasmDevice::DEVICE_NAME, BleWasmDevice::get_instance(),
    BROOKESIA_HAL_WASM_BLE_DEVICE_PLUGIN_SYMBOL
);
#endif

} // namespace esp_brookesia::hal
