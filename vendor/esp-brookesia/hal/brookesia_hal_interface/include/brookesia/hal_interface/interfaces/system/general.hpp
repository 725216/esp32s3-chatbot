/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <expected>
#include <string>

#include "brookesia/hal_interface/interface.hpp"

namespace esp_brookesia::hal::system {

/**
 * @brief General system control provided by the platform HAL.
 */
class GeneralIface: public Interface {
public:
    static constexpr const char *NAME = "SystemGeneral";

    GeneralIface()
        : Interface(NAME)
    {
    }

    ~GeneralIface() override = default;

    /**
     * @brief Restart the host device or simulator.
     *
     * @return Empty value on successful hand-off, or a diagnostic error when the
     * platform cannot perform a restart.
     */
    virtual std::expected<void, std::string> restart() = 0;
};

} // namespace esp_brookesia::hal::system
