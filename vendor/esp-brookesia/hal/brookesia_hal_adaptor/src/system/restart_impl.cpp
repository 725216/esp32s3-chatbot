/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/hal_adaptor/macro_configs.h"

#if BROOKESIA_HAL_ADAPTOR_SYSTEM_ENABLE_RESTART_IMPL
#include <memory>

#include "esp_system.h"

#include "brookesia/hal_interface/interfaces/system/general.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::hal {

class RestartAdaptorImpl final: public system::GeneralIface {
public:
    std::expected<void, std::string> restart() override
    {
        BROOKESIA_LOGI("Restarting ESP device");
        esp_restart();
        return {};
    }
};

std::shared_ptr<system::GeneralIface> make_restart_adaptor_iface()
{
    return std::make_shared<RestartAdaptorImpl>();
}

} // namespace esp_brookesia::hal
#endif
