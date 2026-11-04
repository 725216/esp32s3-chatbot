/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_manager/detail/static_schema.hpp"
#include "brookesia/service_manager/helper/base.hpp"

namespace esp_brookesia::service::helper {

/**
 * @brief Helper schema definitions for the SNTP service.
 */
class SNTP: public Base<SNTP> {
public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////// The following are the service specific types and enumerations ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief SNTP synchronization state.
     */
    enum class State {
        Stopped,
        CheckingNetwork,
        Syncing,
        Synced,
        Max,
    };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the types required by the Base class /////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief SNTP service function identifiers.
     */
    enum class FunctionId {
        SetServers,
        SetTimezone,
        Start,
        Stop,
        GetServers,
        GetTimezone,
        GetState,
        IsTimeSynced,
        LoadData,
        ResetData,
        Max,
    };

    /**
     * @brief SNTP service event identifiers.
     */
    enum class EventId {
        StateChanged,
        TimezoneChanged,
        Max,
    };

    /**
     * @brief Parameter keys for `FunctionId::SetServers`.
     */

    /**
     * @brief Parameter keys for `FunctionId::SetTimezone`.
     */

    /**
     * @brief Event keys for `EventId::StateChanged`.
     */

    /**
     * @brief Event keys for `EventId::TimezoneChanged`.
     */

private:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the static schema specifications ////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    using EventItemSpec = detail::static_schema::EventItemSpec;
    using EventSpec = detail::static_schema::EventSpec;
    using FunctionParameterSpec = detail::static_schema::FunctionParameterSpec;
    using FunctionReturnSpec = detail::static_schema::FunctionReturnSpec;
    using FunctionSpec = detail::static_schema::FunctionSpec;

    inline static constexpr std::span<const FunctionParameterSpec> EMPTY_PARAMETERS = {};

    inline static constexpr std::array<FunctionParameterSpec, 1> SET_SERVERS_PARAMETERS = {{
            {
                .name = "Servers",
                .description = R"(NTP servers as JSON array<string>. Example: ["pool.ntp.org","cn.pool.ntp.org"])",
                .type = FunctionValueType::Array,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> SET_TIMEZONE_PARAMETERS = {{
            {
                .name = "Timezone",
                .description = "Timezone string.",
                .type = FunctionValueType::String,
            },
        }
    };

    inline static constexpr std::array<FunctionSpec, static_cast<std::size_t>(FunctionId::Max)> FUNCTION_SPECS = {{
            {
                .name = "SetServers",
                .description = "Set NTP servers.",
                .parameters = SET_SERVERS_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "SetTimezone",
                .description = "Set timezone.",
                .parameters = SET_TIMEZONE_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "Start",
                .description = "Start SNTP service.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "Stop",
                .description = "Stop SNTP service.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "GetServers",
                .description = "Get NTP servers.",
                .parameters = EMPTY_PARAMETERS,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Array,
                    .description = R"(Example: ["pool.ntp.org","cn.pool.ntp.org"])",
                },
            },
            {
                .name = "GetTimezone",
                .description = "Get timezone.",
                .parameters = EMPTY_PARAMETERS,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::String,
                    .description = R"(Example: "CST-8")",
                },
            },
            {
                .name = "GetState",
                .description = "Get SNTP synchronization state.",
                .parameters = EMPTY_PARAMETERS,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::String,
                    .description = R"(Example: "CheckingNetwork")",
                },
            },
            {
                .name = "IsTimeSynced",
                .description = "Check whether time is synced.",
                .parameters = EMPTY_PARAMETERS,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Boolean,
                    .description = "Example: true",
                },
            },
            {
                .name = "LoadData",
                .description = "Load persisted NTP servers and timezone.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "ResetData",
                .description = "Reset NTP servers, timezone, and sync status.",
                .parameters = EMPTY_PARAMETERS,
            },
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> STATE_CHANGED_ITEMS = {{
            {
                .name = "State",
                .description = "Current SNTP synchronization state.",
                .type = EventItemType::String,
            },
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> TIMEZONE_CHANGED_ITEMS = {{
            {
                .name = "Timezone",
                .description = "Current timezone string.",
                .type = EventItemType::String,
            },
        }
    };

    inline static constexpr std::array<EventSpec, static_cast<std::size_t>(EventId::Max)> EVENT_SPECS = {{
            {
                .name = "StateChanged",
                .description = "Published when the SNTP synchronization state changes.",
                .items = STATE_CHANGED_ITEMS,
            },
            {
                .name = "TimezoneChanged",
                .description = "Published when the SNTP timezone changes.",
                .items = TIMEZONE_CHANGED_ITEMS,
            },
        }
    };

    static_assert(FUNCTION_SPECS.size() == static_cast<std::size_t>(FunctionId::Max));
    static_assert(EVENT_SPECS.size() == static_cast<std::size_t>(EventId::Max));

public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the functions required by the Base class /////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Name of the SNTP service.
     *
     * @return std::string_view Stable service name.
     */
    static constexpr std::string_view get_name()
    {
        return "SNTP";
    }

    /**
     * @brief Get the function schemas exported by the SNTP service.
     *
     * @return std::span<const FunctionSchema> Static schema span.
     */
    static std::span<const FunctionSchema> get_function_schemas()
    {
        static std::array<FunctionSchema, FUNCTION_SPECS.size()> schemas;
        static const bool initialized = [] {
            detail::static_schema::materialize_function_schemas(FUNCTION_SPECS, schemas);
            return true;
        }();
        static_cast<void>(initialized);
        return schemas;
    }

    /**
     * @brief Get the event schemas exported by the SNTP service.
     *
     * @return std::span<const EventSchema> Static event schema span.
     */
    static std::span<const EventSchema> get_event_schemas()
    {
        static std::array<EventSchema, EVENT_SPECS.size()> schemas;
        static const bool initialized = [] {
            detail::static_schema::materialize_event_schemas(EVENT_SPECS, schemas);
            return true;
        }();
        static_cast<void>(initialized);
        return schemas;
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the describe macros //////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BROOKESIA_DESCRIBE_ENUM(SNTP::State, Stopped, CheckingNetwork, Syncing, Synced, Max);
BROOKESIA_DESCRIBE_ENUM(
    SNTP::FunctionId, SetServers, SetTimezone, Start, Stop, GetServers, GetTimezone, GetState, IsTimeSynced,
    LoadData, ResetData, Max
);
BROOKESIA_DESCRIBE_ENUM(SNTP::EventId, StateChanged, TimezoneChanged, Max);

} // namespace esp_brookesia::service::helper
