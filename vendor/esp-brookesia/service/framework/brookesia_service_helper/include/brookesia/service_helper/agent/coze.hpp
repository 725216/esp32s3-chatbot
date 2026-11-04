/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <array>
#include <span>

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_manager/detail/static_schema.hpp"
#include "brookesia/service_manager/helper/base.hpp"
#include "manager.hpp"

namespace esp_brookesia::service::helper {

/**
 * @brief Helper schema definitions for the Coze agent service.
 */
class Coze: public service::helper::Base<Coze> {
public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////// The following are the service specific types and enumerations ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Credentials required to authenticate with the Coze backend.
     */
    struct AuthInfo {
        std::string session_name; ///< Session name used by the Coze SDK.
        std::string device_id; ///< Unique device identifier.
        std::string custom_consumer; ///< Consumer identifier passed to the backend.
        std::string app_id; ///< Application id.
        std::string user_id; ///< End-user id.
        std::string public_key; ///< Public key used for authentication.
        std::string private_key; ///< Private key used for authentication.
    };

    /**
     * @brief Metadata for one available Coze robot.
     */
    struct RobotInfo {
        std::string name; ///< Human-readable robot name.
        std::string bot_id; ///< Coze bot identifier.
        std::string voice_id; ///< Voice profile identifier.
        std::string description; ///< Optional robot description.
    };

    /**
     * @brief Runtime Coze agent configuration.
     */
    struct Info {
        AuthInfo authorization; ///< Authentication material.
        std::vector<RobotInfo> robots; ///< Available robot definitions.
    };

    /**
     * @brief Coze-specific events surfaced by the agent.
     */
    enum class CozeEvent {
        InsufficientCreditsBalance,
        Max,
    };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the types required by the Base class /////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    enum class FunctionId : uint8_t {
        SetActiveRobotIndex,
        GetActiveRobotIndex,
        GetRobotInfos,
        Max,
    };

    enum class EventId : uint8_t {
        CozeEventHappened,
        Max,
    };

private:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the function schemas /////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    using FunctionParameterSpec = service::detail::static_schema::FunctionParameterSpec;
    using FunctionReturnSpec = service::detail::static_schema::FunctionReturnSpec;
    using FunctionSpec = service::detail::static_schema::FunctionSpec;

    inline static constexpr std::array<FunctionParameterSpec, 1> SET_ACTIVE_ROBOT_INDEX_PARAMETERS = {{
            {
                .name = "Index",
                .description = "Robot index to activate.",
                .type = service::FunctionValueType::Number,
            },
        }
    };

    inline static constexpr std::array<FunctionSpec, static_cast<size_t>(FunctionId::Max)> FUNCTION_SPECS = {{
            {
                .name = "SetActiveRobotIndex",
                .description = "Set active robot index.",
                .parameters = SET_ACTIVE_ROBOT_INDEX_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "GetActiveRobotIndex",
                .description = "Get active robot index.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::Number,
                    .description = "Example: 0",
                },
            },
            {
                .name = "GetRobotInfos",
                .description = "Get robot info list.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::Array,
                    .description =
                    "Example: [{\"name\":\"robot1\",\"bot_id\":\"bot_id1\",\"voice_id\":\"voice_id1\","
                    "\"description\":\"description1\"},{\"name\":\"robot2\",\"bot_id\":\"bot_id2\","
                    "\"voice_id\":\"voice_id2\",\"description\":\"description2\"}]",
                },
            },
        }
    };
    static_assert(FUNCTION_SPECS.size() == static_cast<size_t>(FunctionId::Max));

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the event schemas /////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    using EventItemSpec = service::detail::static_schema::EventItemSpec;
    using EventSpec = service::detail::static_schema::EventSpec;

    inline static constexpr std::array<EventItemSpec, 1> COZE_EVENT_HAPPENED_ITEMS = {{
            {"CozeEvent", "Coze event. Allowed values: [InsufficientCreditsBalance]", service::EventItemType::String},
        }
    };

    inline static constexpr std::array<EventSpec, static_cast<size_t>(EventId::Max)> EVENT_SPECS = {{
            {
                .name = "CozeEventHappened",
                .description = "Emitted when a Coze event occurs.",
                .items = COZE_EVENT_HAPPENED_ITEMS,
            },
        }
    };
    static_assert(EVENT_SPECS.size() == static_cast<size_t>(EventId::Max));

public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the functions required by the Base class /////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Name of the Coze agent service.
     *
     * @return std::string_view Stable service name.
     */
    static constexpr std::string_view get_name()
    {
        return "Coze";
    }

    /**
     * @brief Get the function schemas exported by the Coze agent.
     *
     * @return std::span<const service::FunctionSchema> Static schema span.
     */
    static std::span<const service::FunctionSchema> get_function_schemas()
    {
        static std::array<service::FunctionSchema, FUNCTION_SPECS.size()> schemas;
        static const bool initialized = [] {
            service::detail::static_schema::materialize_function_schemas(FUNCTION_SPECS, schemas);
            return true;
        }();
        static_cast<void>(initialized);
        return schemas;
    }

    /**
     * @brief Get the event schemas exported by the Coze agent.
     *
     * @return std::span<const service::EventSchema> Static schema span.
     */
    static std::span<const service::EventSchema> get_event_schemas()
    {
        static std::array<service::EventSchema, EVENT_SPECS.size()> schemas;
        static const bool initialized = [] {
            service::detail::static_schema::materialize_event_schemas(EVENT_SPECS, schemas);
            return true;
        }();
        static_cast<void>(initialized);
        return schemas;
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the describe macros //////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BROOKESIA_DESCRIBE_STRUCT(
    Coze::AuthInfo, (), (session_name, device_id, custom_consumer, app_id, user_id, public_key, private_key)
);
BROOKESIA_DESCRIBE_STRUCT(
    Coze::RobotInfo, (), (name, bot_id, voice_id, description)
);
BROOKESIA_DESCRIBE_STRUCT(
    Coze::Info, (), (authorization, robots)
);
BROOKESIA_DESCRIBE_ENUM(Coze::CozeEvent, InsufficientCreditsBalance, Max);
BROOKESIA_DESCRIBE_ENUM(Coze::FunctionId, SetActiveRobotIndex, GetActiveRobotIndex, GetRobotInfos, Max);
BROOKESIA_DESCRIBE_ENUM(Coze::EventId, CozeEventHappened, Max);

} // namespace esp_brookesia::service::helper
