/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "brookesia/lib_utils/task_scheduler_describe.hpp"
#include "brookesia/service_manager/service/base.hpp"

namespace esp_brookesia::service {

BROOKESIA_DESCRIBE_ENUM(ServiceBase::SchedulerType, Main, Secondary)
BROOKESIA_DESCRIBE_STRUCT(
    ServiceBase::Attributes, (), (name, description, version, dependencies, task_scheduler_config, scheduler_type)
)

} // namespace esp_brookesia::service
