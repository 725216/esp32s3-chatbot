/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/lib_utils/thread_config.hpp"

namespace esp_brookesia::lib_utils {

BROOKESIA_DESCRIBE_STRUCT(ThreadConfig, (), (name, core_id, priority, stack_size, stack_in_ext))

} // namespace esp_brookesia::lib_utils
