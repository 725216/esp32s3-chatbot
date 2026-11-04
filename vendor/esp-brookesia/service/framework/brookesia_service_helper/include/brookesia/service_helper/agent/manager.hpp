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
#include "brookesia/service_helper/media/audio.hpp"

namespace esp_brookesia::service::helper {

/**
 * @brief Helper schema definitions for the agent manager service.
 */
class AgentManager: public service::helper::Base<AgentManager> {
public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////// The following are the service specific types and enumerations ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Timeout budget for the major lifecycle operations of an agent.
     */
    struct AgentOperationTimeout {
        uint32_t activate = 1000; ///< Timeout for activation.
        uint32_t start = 1000; ///< Timeout for startup.
        uint32_t sleep = 1000; ///< Timeout for sleep.
        uint32_t wake_up = 1000; ///< Timeout for wake-up.
        uint32_t stop = 1000; ///< Timeout for shutdown.
    };

    /**
     * @brief Optional per-agent functions surfaced by the manager.
     */
    enum class AgentGeneralFunction : uint8_t {
        InterruptSpeaking,
        Max,
    };

    /**
     * @brief Optional per-agent events surfaced by the manager.
     */
    enum class AgentGeneralEvent : uint8_t {
        SpeakingStatusChanged,
        ListeningStatusChanged,
        AgentSpeakingTextGot,
        UserSpeakingTextGot,
        EmoteGot,
        Max,
    };

    /**
     * @brief Conversation mode used by the active agent.
     */
    enum class ChatMode : uint8_t {
        RealTime,   ///< Real-time conversation mode. Agent can listen while speaking.
        Manual,     ///< Manual conversation mode. User can start and stop listening manually.
        HalfDuplex, ///< Half-duplex conversation mode. Agent can only speak or only listen.
        Max,
    };

    /**
     * @brief Public metadata describing one agent implementation.
     */
    struct AgentAttributes {
        /**
         * @brief Get the agent name stored in the attributes.
         *
         * @return std::string Agent name.
         */
        std::string get_name() const
        {
            return name;
        }

        /**
         * @brief Check whether an optional general function is supported.
         *
         * @param[in] function Function to check.
         * @return true if the function is listed in `support_general_functions`.
         */
        bool is_general_functions_supported(AgentGeneralFunction function) const
        {
            return std::find(support_general_functions.begin(), support_general_functions.end(), function) !=
                   support_general_functions.end();
        }

        /**
         * @brief Check whether an optional general event is supported.
         *
         * @param[in] event Event to check.
         * @return true if the event is listed in `support_general_events`.
         */
        bool is_general_events_supported(AgentGeneralEvent event) const
        {
            return std::find(support_general_events.begin(), support_general_events.end(), event) !=
                   support_general_events.end();
        }

        std::string name; ///< Agent name exposed to the manager.
        AgentOperationTimeout operation_timeout{}; ///< Timeout configuration for lifecycle actions.
        std::vector<AgentGeneralFunction> support_general_functions{}; ///< Optional functions supported by the agent.
        std::vector<AgentGeneralEvent> support_general_events{}; ///< Optional events emitted by the agent.
        bool require_time_sync = false; ///< Whether activation requires SNTP synchronization first.
    };

    /**
     * @brief High-level actions accepted by the agent manager.
     */
    enum class GeneralAction : uint8_t {
        TimeSync,
        Activate,
        Start,
        Sleep,
        WakeUp,
        Stop,
        Max,
    };

    /**
     * @brief High-level completion events emitted by managed agents.
     */
    enum class GeneralEvent : uint8_t {
        TimeSynced,
        Activated,
        Started,
        Slept,
        Awake,
        Stopped,
        Max,
    };

    /**
     * @brief State-machine states used by the agent manager.
     */
    enum class GeneralState : uint8_t {
        TimeSyncing,
        Ready,
        Activating,
        Activated,
        Starting,
        Started,
        Sleeping,
        Slept,
        WakingUp,
        Stopping,
        Max
    };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the types required by the Base class /////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    enum class FunctionId : uint8_t {
        GetAgentNames,
        GetAgentAttributes,
        SetAgentInfo,
        SetTargetAgent,
        GetTargetAgent,
        GetActiveAgent,
        TriggerGeneralAction,
        GetGeneralState,
        InterruptSpeaking,
        Suspend,
        Resume,
        GetSuspendStatus,
        SetChatMode,
        GetChatMode,
        ManualStartListening,
        ManualStopListening,
        GetSpeakingStatus,
        GetListeningStatus,
        LoadData,
        ResetData,
        Max,
    };

    enum class EventId : uint8_t {
        GeneralActionTriggered,
        GeneralEventHappened,
        SuspendStatusChanged,
        SpeakingStatusChanged,
        ListeningStatusChanged,
        AgentSpeakingTextGot,
        UserSpeakingTextGot,
        EmoteGot,
        Max,
    };














private:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the function schemas /////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    using DefaultValueKind = service::detail::static_schema::DefaultValueKind;
    using DefaultValueSpec = service::detail::static_schema::DefaultValueSpec;
    using FunctionParameterSpec = service::detail::static_schema::FunctionParameterSpec;
    using FunctionReturnSpec = service::detail::static_schema::FunctionReturnSpec;
    using FunctionSpec = service::detail::static_schema::FunctionSpec;

    inline static constexpr DefaultValueSpec EMPTY_STRING_DEFAULT = {
        .kind = DefaultValueKind::String,
        .string = "",
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> SET_AGENT_INFO_PARAMETERS = {{
            {
                .name = "Name",
                .description = "Agent name.",
                .type = service::FunctionValueType::String,
            },
            {
                .name = "Info",
                .description =
                R"(Agent info as a JSON object. Example: {"api_key":"api_key_value",)"
                R"("model":"model_name"})",
                .type = service::FunctionValueType::Object,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> SET_CHAT_MODE_PARAMETERS = {{
            {
                .name = "Mode",
                .description = "Chat mode. Allowed values: [Manual, RealTime]",
                .type = service::FunctionValueType::String,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> SET_TARGET_AGENT_PARAMETERS = {{
            {
                .name = "Name",
                .description = "Agent name to set as target.",
                .type = service::FunctionValueType::String,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> GET_AGENT_ATTRIBUTES_PARAMETERS = {{
            {
                .name = "Name",
                .description = "Agent name (optional). Returns all agents when omitted.",
                .type = service::FunctionValueType::String,
                .default_value = EMPTY_STRING_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> TRIGGER_GENERAL_ACTION_PARAMETERS = {{
            {
                .name = "Action",
                .description = "General action. Allowed values: [TimeSync, Activate, Start, Stop, Sleep, WakeUp]",
                .type = service::FunctionValueType::String,
            },
        }
    };

    // Preserve the public schema order, which intentionally differs from FunctionId's numeric order.
    inline static constexpr std::array<FunctionSpec, static_cast<size_t>(FunctionId::Max)> FUNCTION_SPECS = {{
            {
                .name = "SetAgentInfo",
                .description = "Set agent info.",
                .parameters = SET_AGENT_INFO_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "SetChatMode",
                .description = "Set chat mode.",
                .parameters = SET_CHAT_MODE_PARAMETERS,
            },
            {
                .name = "SetTargetAgent",
                .description = "Set target agent. The agent will be activated by triggering `Activate` action.",
                .parameters = SET_TARGET_AGENT_PARAMETERS,
            },
            {
                .name = "GetTargetAgent",
                .description = "Get target agent name.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::String,
                    .description = "Example: \"Coze\"",
                },
            },
            {
                .name = "GetAgentNames",
                .description = "Get agent names.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::Array,
                    .description = "Example: [\"Agent1\", \"Agent2\"]",
                },
            },
            {
                .name = "GetAgentAttributes",
                .description = "Get agent attributes.",
                .parameters = GET_AGENT_ATTRIBUTES_PARAMETERS,
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::Array,
                    .description =
                    R"(Example: [{"name":"Agent","operation_timeout":{"activate":1000,"start":1000,"sleep":1000,)"
                    R"("wake_up":1000,"stop":1000},"support_general_functions":[],"support_general_events":[],)"
                    R"("require_time_sync":false}])",
                },
            },
            {
                .name = "GetChatMode",
                .description = "Get chat mode.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::String,
                    .description = "Allowed values: [Manual, RealTime]. Example: \"Manual\"",
                },
            },
            {
                .name = "GetActiveAgent",
                .description = "Get active agent name.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::String,
                    .description = "Example: \"Coze\"",
                },
            },
            {
                .name = "TriggerGeneralAction",
                .description = "Trigger a general action.",
                .parameters = TRIGGER_GENERAL_ACTION_PARAMETERS,
            },
            {
                .name = "Suspend",
                .description = "Suspend the agent.",
                .parameters = std::span<const FunctionParameterSpec>{},
            },
            {
                .name = "Resume",
                .description = "Resume the agent.",
                .parameters = std::span<const FunctionParameterSpec>{},
            },
            {
                .name = "InterruptSpeaking",
                .description = "Interrupt speaking; the agent stops and keeps listening.",
                .parameters = std::span<const FunctionParameterSpec>{},
            },
            {
                .name = "ManualStartListening",
                .description = "Manually start listening. Only in `Manual` mode.",
                .parameters = std::span<const FunctionParameterSpec>{},
            },
            {
                .name = "ManualStopListening",
                .description = "Manually stop listening. Only in `Manual` mode.",
                .parameters = std::span<const FunctionParameterSpec>{},
            },
            {
                .name = "GetGeneralState",
                .description = "Get general state.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::String,
                    .description =
                    "Allowed values: [TimeSyncing, Ready, Activating, Activated, Starting, Stopping, Started, "
                    "Sleeping, WakingUp, Slept]. Example: \"Started\"",
                },
            },
            {
                .name = "GetSuspendStatus",
                .description = "Get suspend status.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::Boolean,
                    .description = "Example: false",
                },
            },
            {
                .name = "GetSpeakingStatus",
                .description = "Get speaking status.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::Boolean,
                    .description = "Example: true",
                },
            },
            {
                .name = "GetListeningStatus",
                .description = "Get listening status.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::Boolean,
                    .description = "Example: true",
                },
            },
            {
                .name = "LoadData",
                .description = "Load persisted manager data.",
                .parameters = std::span<const FunctionParameterSpec>{},
            },
            {
                .name = "ResetData",
                .description = "Reset manager and agent data.",
                .parameters = std::span<const FunctionParameterSpec>{},
            },
        }
    };
    static_assert(FUNCTION_SPECS.size() == static_cast<size_t>(FunctionId::Max));

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the event schemas /////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    using EventItemSpec = service::detail::static_schema::EventItemSpec;
    using EventSpec = service::detail::static_schema::EventSpec;

    inline static constexpr std::array<EventItemSpec, 1> GENERAL_ACTION_TRIGGERED_ITEMS = {{
            {
                "Action", "General action. Allowed values: [TimeSync, Activate, Start, Stop, Sleep, WakeUp]",
                service::EventItemType::String
            },
        }
    };

    inline static constexpr std::array<EventItemSpec, 2> GENERAL_EVENT_HAPPENED_ITEMS = {{
            {
                "Event", "General event. Allowed values: [TimeSynced, Activated, Started, Stopped, Slept, Awake]",
                service::EventItemType::String
            },
            {"IsUnexpected", "Whether the event was unexpected.", service::EventItemType::Boolean},
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> SUSPEND_STATUS_CHANGED_ITEMS = {{
            {"IsSuspended", "Whether the agent is suspended.", service::EventItemType::Boolean},
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> SPEAKING_STATUS_CHANGED_ITEMS = {{
            {"IsSpeaking", "Whether the agent is speaking.", service::EventItemType::Boolean},
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> LISTENING_STATUS_CHANGED_ITEMS = {{
            {"IsListening", "Whether the agent is listening.", service::EventItemType::Boolean},
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> AGENT_SPEAKING_TEXT_GOT_ITEMS = {{
            {"Text", "Spoken text.", service::EventItemType::String},
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> USER_SPEAKING_TEXT_GOT_ITEMS = {{
            {"Text", "User text.", service::EventItemType::String},
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> EMOTE_GOT_ITEMS = {{
            {"Emote", "Emote name.", service::EventItemType::String},
        }
    };

    inline static constexpr std::array<EventSpec, static_cast<size_t>(EventId::Max)> EVENT_SPECS = {{
            {
                .name = "GeneralActionTriggered",
                .description = "Emitted when a general action is triggered.",
                .items = GENERAL_ACTION_TRIGGERED_ITEMS,
            },
            {
                .name = "GeneralEventHappened",
                .description = "Emitted when a general event occurs.",
                .items = GENERAL_EVENT_HAPPENED_ITEMS,
            },
            {
                .name = "SuspendStatusChanged",
                .description = "Emitted when suspend status changes.",
                .items = SUSPEND_STATUS_CHANGED_ITEMS,
            },
            {
                .name = "SpeakingStatusChanged",
                .description = "Emitted when speaking status changes.",
                .items = SPEAKING_STATUS_CHANGED_ITEMS,
            },
            {
                .name = "ListeningStatusChanged",
                .description = "Emitted when listening status changes.",
                .items = LISTENING_STATUS_CHANGED_ITEMS,
            },
            {
                .name = "AgentSpeakingTextGot",
                .description = "Emitted when the agent speaks text.",
                .items = AGENT_SPEAKING_TEXT_GOT_ITEMS,
            },
            {
                .name = "UserSpeakingTextGot",
                .description = "Emitted when the user speaks text.",
                .items = USER_SPEAKING_TEXT_GOT_ITEMS,
            },
            {
                .name = "EmoteGot",
                .description = "Emitted when the agent shows an emote.",
                .items = EMOTE_GOT_ITEMS,
            },
        }
    };
    static_assert(EVENT_SPECS.size() == static_cast<size_t>(EventId::Max));

public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the functions required by the Base class /////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Name of the agent-manager service.
     *
     * @return std::string_view Stable service name.
     */
    static constexpr std::string_view get_name()
    {
        return "AgentManager";
    }

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
/**
 * @brief  Agent related
 */
BROOKESIA_DESCRIBE_STRUCT(AgentManager::AgentOperationTimeout, (), (activate, start, sleep, wake_up, stop));
BROOKESIA_DESCRIBE_ENUM(AgentManager::AgentGeneralFunction, InterruptSpeaking, Max);
BROOKESIA_DESCRIBE_ENUM(AgentManager::ChatMode, RealTime, Manual, HalfDuplex, Max);
BROOKESIA_DESCRIBE_ENUM(
    AgentManager::AgentGeneralEvent, SpeakingStatusChanged, ListeningStatusChanged, AgentSpeakingTextGot,
    UserSpeakingTextGot, EmoteGot, Max
);
BROOKESIA_DESCRIBE_STRUCT(
    AgentManager::AgentAttributes, (), (
        name, operation_timeout, support_general_functions, support_general_events, require_time_sync
    )
);
BROOKESIA_DESCRIBE_ENUM(AgentManager::GeneralAction, TimeSync, Activate, Start, Sleep, WakeUp, Stop, Max);
BROOKESIA_DESCRIBE_ENUM(AgentManager::GeneralEvent, TimeSynced, Activated, Started, Slept, Awake, Stopped, Max);
BROOKESIA_DESCRIBE_ENUM(
    AgentManager::GeneralState, TimeSyncing, Ready, Activating, Activated, Starting, Started, Sleeping, Slept, WakingUp,
    Stopping, Max
);

/**
 * @brief  Function related
 */
BROOKESIA_DESCRIBE_ENUM(
    AgentManager::FunctionId,
    GetAgentNames, GetAgentAttributes, SetAgentInfo, SetTargetAgent, GetTargetAgent, GetActiveAgent,
    TriggerGeneralAction, GetGeneralState, InterruptSpeaking, Suspend, Resume, GetSuspendStatus,
    SetChatMode, GetChatMode, ManualStartListening, ManualStopListening, GetSpeakingStatus, GetListeningStatus,
    LoadData, ResetData,
    Max
);

/**
 * @brief  Event related
 */
BROOKESIA_DESCRIBE_ENUM(
    AgentManager::EventId, GeneralActionTriggered, GeneralEventHappened, SuspendStatusChanged,
    SpeakingStatusChanged, ListeningStatusChanged, AgentSpeakingTextGot, UserSpeakingTextGot, EmoteGot, Max
);

} // namespace esp_brookesia::service::helper
