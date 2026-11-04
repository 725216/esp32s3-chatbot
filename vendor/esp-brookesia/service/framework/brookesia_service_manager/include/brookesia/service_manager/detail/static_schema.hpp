/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "brookesia/service_manager/event/definition.hpp"
#include "brookesia/service_manager/function/definition.hpp"

namespace esp_brookesia::service::detail::static_schema {

struct EventItemSpec {
    const char *name;
    const char *description;
    EventItemType type;
};

struct EventSpec {
    const char *name;
    const char *description;
    std::span<const EventItemSpec> items;
    bool require_scheduler = true;
};

void materialize_event_schemas(std::span<const EventSpec> specs, std::span<EventSchema> schemas);

enum class DefaultValueKind {
    None,
    Bool,
    Number,
    String,
    JsonObject,
    JsonArray,
};

struct DefaultValueSpec {
    DefaultValueKind kind = DefaultValueKind::None;
    bool boolean = false;
    double number = 0;
    const char *string = nullptr;
};

struct FunctionParameterSpec {
    const char *name;
    const char *description;
    FunctionValueType type;
    DefaultValueSpec default_value = {};
};

struct FunctionReturnSpec {
    FunctionValueType type;
    const char *description;
};

struct FunctionSpec {
    const char *name;
    const char *description;
    std::span<const FunctionParameterSpec> parameters;
    bool require_scheduler = true;
    std::optional<uint32_t> default_timeout_ms = std::nullopt;
    std::optional<FunctionReturnSpec> return_value = std::nullopt;
};

void materialize_function_schemas(std::span<const FunctionSpec> specs, std::span<FunctionSchema> schemas);

} // namespace esp_brookesia::service::detail::static_schema
