/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host_coordinator.hpp"

namespace esp_brookesia::hal::bluetooth::detail {

BluetoothHostCoordinator &BluetoothHostCoordinator::get_instance()
{
    static BluetoothHostCoordinator coordinator;
    return coordinator;
}

BluetoothHostCoordinator::Token BluetoothHostCoordinator::acquire(Profile profile)
{
    std::lock_guard lock(mutex_);
    if (profile == Profile::Ble) {
        ++ble_users_;
    } else {
        ++classic_users_;
    }
    return Token(this, profile);
}

size_t BluetoothHostCoordinator::active_profiles() const
{
    std::lock_guard lock(mutex_);
    return ble_users_ + classic_users_;
}

size_t BluetoothHostCoordinator::active(Profile profile) const
{
    std::lock_guard lock(mutex_);
    return profile == Profile::Ble ? ble_users_ : classic_users_;
}

void BluetoothHostCoordinator::release(Profile profile)
{
    std::lock_guard lock(mutex_);
    auto &users = profile == Profile::Ble ? ble_users_ : classic_users_;
    if (users > 0) {
        --users;
    }
}

void BluetoothHostCoordinator::Token::reset()
{
    if (owner_ != nullptr) {
        owner_->release(profile_);
        owner_ = nullptr;
    }
}

} // namespace esp_brookesia::hal::bluetooth::detail
