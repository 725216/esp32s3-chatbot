/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <mutex>

namespace esp_brookesia::hal::bluetooth::detail {

/**
 * Ref-counted ownership for profiles sharing one ESP Bluetooth controller/host.
 *
 * The coordinator intentionally does not start a controller by itself. Profile backends acquire
 * a token before starting and release it after stopping; the target-specific host adapter owns the
 * actual ESP-IDF init/deinit calls. This prevents one profile from tearing down a host still used
 * by another profile during fast stop/start sequences.
 */
class BluetoothHostCoordinator {
public:
    enum class Profile { Ble, Classic };

    class Token {
    public:
        Token() = default;
        Token(BluetoothHostCoordinator *owner, Profile profile)
            : owner_(owner), profile_(profile)
        {
        }
        Token(const Token &) = delete;
        Token &operator=(const Token &) = delete;
        Token(Token &&other) noexcept
            : owner_(other.owner_), profile_(other.profile_)
        {
            other.owner_ = nullptr;
        }
        Token &operator=(Token &&other) noexcept
        {
            if (this != &other) {
                reset();
                owner_ = other.owner_;
                profile_ = other.profile_;
                other.owner_ = nullptr;
            }
            return *this;
        }
        ~Token()
        {
            reset();
        }

        void reset();
        explicit operator bool() const
        {
            return owner_ != nullptr;
        }

    private:
        BluetoothHostCoordinator *owner_ = nullptr;
        Profile profile_ = Profile::Ble;
    };

    static BluetoothHostCoordinator &get_instance();
    Token acquire(Profile profile);
    size_t active_profiles() const;
    size_t active(Profile profile) const;

private:
    void release(Profile profile);

    mutable std::mutex mutex_;
    size_t ble_users_ = 0;
    size_t classic_users_ = 0;
};

} // namespace esp_brookesia::hal::bluetooth::detail
