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

class BoardInfoLinuxImpl;
class OtaUpdaterLinuxIface;
class RestartLinuxImpl;

class SystemLinuxDevice: public Device {
public:
    static constexpr const char *DEVICE_NAME = "SystemLinux";
    static constexpr const char *BOARD_INFO_IFACE_NAME = "SystemLinux:BoardInfo";
    static constexpr const char *OTA_UPDATER_IFACE_NAME = "SystemLinux:OtaUpdater";
    static constexpr const char *RESTART_IFACE_NAME = "SystemLinux:Restart";
    using RestartHandler = std::function<std::expected<void, std::string>()>;

    SystemLinuxDevice(const SystemLinuxDevice &) = delete;
    SystemLinuxDevice &operator=(const SystemLinuxDevice &) = delete;
    SystemLinuxDevice(SystemLinuxDevice &&) = delete;
    SystemLinuxDevice &operator=(SystemLinuxDevice &&) = delete;

    static SystemLinuxDevice &get_instance()
    {
        static SystemLinuxDevice instance;
        return instance;
    }

    /**
     * @brief Register the simulator host restart callback.
     *
     * An empty callback makes the restart interface report an unavailable error.
     */
    void set_restart_handler(RestartHandler handler);

private:
    SystemLinuxDevice()
        : Device(std::string(DEVICE_NAME))
    {
    }
    ~SystemLinuxDevice() = default;

    bool probe() override;
    std::vector<InterfaceSpec> get_interface_specs() const override;
    bool on_init() override;
    void on_deinit() override;

    std::shared_ptr<BoardInfoLinuxImpl> board_info_iface_;
    std::shared_ptr<OtaUpdaterLinuxIface> ota_updater_iface_;
    std::shared_ptr<RestartLinuxImpl> restart_iface_;
    mutable std::mutex restart_mutex_;
    RestartHandler restart_handler_;
};

} // namespace esp_brookesia::hal
