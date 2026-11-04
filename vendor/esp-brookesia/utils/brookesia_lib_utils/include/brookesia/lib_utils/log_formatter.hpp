/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

namespace esp_brookesia::lib_utils {

/**
 * @brief Optional formatter customization for non-primitive log arguments.
 *
 * Include `log_describe.hpp` (or `describe_helpers.hpp`) to enable the default
 * formatter backed by the describe helpers, or specialize this template for a
 * domain type without pulling reflection and JSON into every logging user.
 */
template<typename T>
struct LogArgumentFormatter;

} // namespace esp_brookesia::lib_utils
