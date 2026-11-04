/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include "brookesia/hal_interface/device.hpp"

namespace esp_brookesia::hal {

class BoardInfoWasmImpl;
class RestartWasmImpl;

class SystemWasmDevice: public Device {
public:
    static constexpr const char *DEVICE_NAME = "SystemWasm";
    static constexpr const char *BOARD_INFO_IFACE_NAME = "SystemWasm:BoardInfo";
    static constexpr const char *RESTART_IFACE_NAME = "SystemWasm:Restart";
    using RestartHandler = std::function<std::expected<void, std::string>()>;

    SystemWasmDevice(const SystemWasmDevice &) = delete;
    SystemWasmDevice &operator=(const SystemWasmDevice &) = delete;
    SystemWasmDevice(SystemWasmDevice &&) = delete;
    SystemWasmDevice &operator=(SystemWasmDevice &&) = delete;

    static SystemWasmDevice &get_instance()
    {
        static SystemWasmDevice instance;
        return instance;
    }

    /**
     * @brief Register a host restart callback; Emscripten also checks
     * `Module.__brookesiaRestart` when no callback is installed.
     */
    void set_restart_handler(RestartHandler handler);

private:
    SystemWasmDevice()
        : Device(std::string(DEVICE_NAME))
    {
    }
    ~SystemWasmDevice() = default;

    bool probe() override;
    std::vector<InterfaceSpec> get_interface_specs() const override;
    bool on_init() override;
    void on_deinit() override;

    std::shared_ptr<BoardInfoWasmImpl> board_info_iface_;
    std::shared_ptr<RestartWasmImpl> restart_iface_;
    mutable std::mutex restart_mutex_;
    RestartHandler restart_handler_;
};

} // namespace esp_brookesia::hal
