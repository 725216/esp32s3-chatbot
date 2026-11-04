/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/service_manager/detail/static_schema.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <utility>

#include "boost/json.hpp"
#include "boost/system/error_code.hpp"

namespace esp_brookesia::service::detail::static_schema {

static std::optional<FunctionValue> materialize_default_value(const DefaultValueSpec &spec);

void materialize_event_schemas(std::span<const EventSpec> specs, std::span<EventSchema> schemas)
{
    assert(specs.size() == schemas.size());

    for (size_t index = 0; index < specs.size(); ++index) {
        const auto &spec = specs[index];
        auto &schema = schemas[index];
        schema.name = spec.name;
        schema.description = spec.description;
        schema.require_scheduler = spec.require_scheduler;
        schema.items.clear();
        schema.items.reserve(spec.items.size());
        for (const auto &item_spec : spec.items) {
            schema.items.emplace_back(EventItemSchema{
                .name = item_spec.name,
                .description = item_spec.description,
                .type = item_spec.type,
            });
        }
    }
}

void materialize_function_schemas(std::span<const FunctionSpec> specs, std::span<FunctionSchema> schemas)
{
    assert(specs.size() == schemas.size());

    for (size_t index = 0; index < specs.size(); ++index) {
        const auto &spec = specs[index];
        auto &schema = schemas[index];
        schema.name = spec.name;
        schema.description = spec.description;
        schema.require_scheduler = spec.require_scheduler;
        schema.default_timeout_ms = spec.default_timeout_ms;
        if (spec.return_value.has_value()) {
            schema.return_value = FunctionReturnSchema{
                .type = spec.return_value->type,
                .description = spec.return_value->description,
            };
        } else {
            schema.return_value.reset();
        }

        schema.parameters.clear();
        schema.parameters.reserve(spec.parameters.size());
        for (const auto &parameter_spec : spec.parameters) {
            FunctionParameterSchema parameter{
                .name = parameter_spec.name,
                .description = parameter_spec.description,
                .type = parameter_spec.type,
            };
            parameter.default_value = materialize_default_value(parameter_spec.default_value);
            schema.parameters.emplace_back(std::move(parameter));
        }
    }
}

static std::optional<FunctionValue> materialize_default_value(const DefaultValueSpec &spec)
{
    switch (spec.kind) {
    case DefaultValueKind::None:
        return std::nullopt;
    case DefaultValueKind::Bool:
        return FunctionValue(spec.boolean);
    case DefaultValueKind::Number:
        return FunctionValue(spec.number);
    case DefaultValueKind::String:
        return FunctionValue(std::string(spec.string));
    case DefaultValueKind::JsonObject: {
        boost::system::error_code error;
        auto value = boost::json::parse(spec.string, error);
        assert(!error && value.is_object());
        if (error || !value.is_object()) {
            return std::nullopt;
        }
        return FunctionValue(std::move(value.as_object()));
    }
    case DefaultValueKind::JsonArray: {
        boost::system::error_code error;
        auto value = boost::json::parse(spec.string, error);
        assert(!error && value.is_array());
        if (error || !value.is_array()) {
            return std::nullopt;
        }
        return FunctionValue(std::move(value.as_array()));
    }
    }

    return std::nullopt;
}

} // namespace esp_brookesia::service::detail::static_schema
