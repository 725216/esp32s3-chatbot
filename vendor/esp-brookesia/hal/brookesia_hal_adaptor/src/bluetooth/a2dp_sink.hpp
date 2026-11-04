/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include "brookesia/hal_interface/interfaces/bluetooth/a2dp_sink.hpp"

namespace esp_brookesia::hal {

/** Create the target-specific A2DP Sink interface. */
std::shared_ptr<bluetooth::A2dpSinkIface> create_a2dp_sink_iface();

} // namespace esp_brookesia::hal
