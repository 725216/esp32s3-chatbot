/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/hal_linux/macro_configs.h"
#if !BROOKESIA_HAL_LINUX_SYSTEM_DEVICE_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/utsname.h>
#include "private/utils.hpp"
#include "brookesia/hal_interface/interfaces/system/board_info.hpp"
#include "brookesia/hal_interface/interfaces/system/general.hpp"
#include "brookesia/hal_linux/system/device.hpp"
#include "ota_updater_impl.hpp"

namespace esp_brookesia::hal {

namespace {

std::string get_linux_release_name()
{
    std::ifstream os_release("/etc/os-release");
    std::string line;
    while (std::getline(os_release, line)) {
        constexpr std::string_view pretty_name_prefix = "PRETTY_NAME=";
        if (!line.starts_with(pretty_name_prefix)) {
            continue;
        }
        std::string value = line.substr(pretty_name_prefix.size());
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    }
    return "Linux";
}

system::BoardInfoIface::Info make_board_info()
{
    struct utsname uts = {};
    const bool has_uname = (uname(&uts) == 0);
    const std::string release_name = get_linux_release_name();
    const std::string machine = has_uname ? uts.machine : "unknown";
    const std::string release = has_uname ? uts.release : "unknown";

    return {
        .name = "ESP-Brookesia Linux",
        .chip = machine,
        .version = release,
        .description = release_name,
        .manufacturer = "Espressif",
    };
}

} // namespace

class BoardInfoLinuxImpl: public system::BoardInfoIface {
public:
    BoardInfoLinuxImpl()
        : system::BoardInfoIface(make_board_info())
    {
    }
};

class RestartLinuxImpl final: public system::GeneralIface {
public:
    explicit RestartLinuxImpl(SystemLinuxDevice::RestartHandler handler)
        : handler_(std::move(handler))
    {
    }

    std::expected<void, std::string> restart() override
    {
        SystemLinuxDevice::RestartHandler handler;
        {
            std::lock_guard lock(mutex_);
            handler = handler_;
        }
        if (!handler) {
            return std::unexpected("Linux simulator restart is unavailable: no host handler registered");
        }
        return handler();
    }

    void set_handler(SystemLinuxDevice::RestartHandler handler)
    {
        std::lock_guard lock(mutex_);
        handler_ = std::move(handler);
    }

private:
    mutable std::mutex mutex_;
    SystemLinuxDevice::RestartHandler handler_;
};

bool SystemLinuxDevice::probe()
{
    return true;
}

std::vector<InterfaceSpec> SystemLinuxDevice::get_interface_specs() const
{
    std::vector<InterfaceSpec> specs = {
        {system::BoardInfoIface::NAME, BOARD_INFO_IFACE_NAME},
        {system::GeneralIface::NAME, RESTART_IFACE_NAME},
    };
#if BROOKESIA_HAL_LINUX_SYSTEM_ENABLE_OTA_UPDATER_IMPL
    specs.push_back({system::OtaUpdaterIface::NAME, OTA_UPDATER_IFACE_NAME});
#endif
    return specs;
}

bool SystemLinuxDevice::on_init()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_CHECK_EXCEPTION_RETURN(
        board_info_iface_ = std::make_shared<BoardInfoLinuxImpl>(), false,
        "Failed to create board info linux"
    );
    BROOKESIA_CHECK_FALSE_RETURN(board_info_iface_->get_info().is_valid(), false, "Board info linux is invalid");

    interfaces_.emplace(BOARD_INFO_IFACE_NAME, board_info_iface_);

    RestartHandler restart_handler;
    {
        std::lock_guard lock(restart_mutex_);
        restart_handler = restart_handler_;
    }
    BROOKESIA_CHECK_EXCEPTION_RETURN(
        restart_iface_ = std::make_shared<RestartLinuxImpl>(std::move(restart_handler)), false,
        "Failed to create restart linux"
    );
    interfaces_.emplace(RESTART_IFACE_NAME, restart_iface_);

#if BROOKESIA_HAL_LINUX_SYSTEM_ENABLE_OTA_UPDATER_IMPL
    BROOKESIA_CHECK_EXCEPTION_RETURN(
        ota_updater_iface_ = std::make_shared<OtaUpdaterLinuxIface>(), false,
        "Failed to create OTA updater linux"
    );
    interfaces_.emplace(OTA_UPDATER_IFACE_NAME, ota_updater_iface_);
#endif

    return true;
}

void SystemLinuxDevice::on_deinit()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    interfaces_.erase(BOARD_INFO_IFACE_NAME);
    board_info_iface_.reset();
    interfaces_.erase(RESTART_IFACE_NAME);
    restart_iface_.reset();
#if BROOKESIA_HAL_LINUX_SYSTEM_ENABLE_OTA_UPDATER_IMPL
    interfaces_.erase(OTA_UPDATER_IFACE_NAME);
    ota_updater_iface_.reset();
#endif
}

void SystemLinuxDevice::set_restart_handler(RestartHandler handler)
{
    std::shared_ptr<RestartLinuxImpl> restart_iface;
    RestartHandler handler_copy;
    {
        std::lock_guard lock(restart_mutex_);
        restart_handler_ = std::move(handler);
        handler_copy = restart_handler_;
        restart_iface = restart_iface_;
    }
    if (restart_iface) {
        restart_iface->set_handler(std::move(handler_copy));
    }
}

#if BROOKESIA_HAL_LINUX_ENABLE_SYSTEM_DEVICE
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    Device, SystemLinuxDevice, SystemLinuxDevice::DEVICE_NAME, SystemLinuxDevice::get_instance(),
    BROOKESIA_HAL_LINUX_SYSTEM_DEVICE_PLUGIN_SYMBOL
);
#endif

} // namespace esp_brookesia::hal
