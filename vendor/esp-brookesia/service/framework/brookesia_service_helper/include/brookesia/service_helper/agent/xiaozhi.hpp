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
#include "brookesia/service_manager/function/definition.hpp"
#include "manager.hpp"

namespace esp_brookesia::service::helper {

/**
 * @brief Helper schema definitions for the XiaoZhi agent service.
 */
class XiaoZhi: public service::helper::Base<XiaoZhi> {
public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////// The following are the service specific types and enumerations ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the types required by the Base class /////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    enum class FunctionId : uint8_t {
        AddMCP_ToolsWithServiceFunction,
        AddMCP_ToolsWithCustomFunction,
        RemoveMCP_Tools,
        ExplainImage,
        Max,
    };

    enum class EventId : uint8_t {
        ActivationCodeReceived,
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

    inline static constexpr DefaultValueSpec EMPTY_ARRAY_DEFAULT = {
        .kind = DefaultValueKind::JsonArray,
        .string = "[]",
    };

    inline static constexpr DefaultValueSpec DEFAULT_IMAGE_QUESTION = {
        .kind = DefaultValueKind::String,
        .string = "What is in the image?",
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> ADD_MCP_TOOLS_WITH_SERVICE_FUNCTION_PARAMETERS = {{
            {
                .name = "ServiceName",
                .description = "Service name.",
                .type = service::FunctionValueType::String,
            },
            {
                .name = "FunctionNames",
                .description =
                "Function names as JSON array<string>. Empty means all functions in the service. Example: "
                "[\"SetAudioPlayerVolume\",\"GetAudioPlayerVolume\"]",
                .type = service::FunctionValueType::Array,
                .default_value = EMPTY_ARRAY_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> ADD_MCP_TOOLS_WITH_CUSTOM_FUNCTION_PARAMETERS = {{
            {
                .name = "Tools",
                .description =
                "Tools to add as JSON array<object>. Example: [{\"description\":\"custom tool description 1\","
                "\"name\":\"Display.GetBrightness\"},{\"description\":\"custom tool description 2\","
                "\"name\":\"Display.SetBrightness\"}]",
                .type = service::FunctionValueType::Array,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> REMOVE_MCP_TOOLS_PARAMETERS = {{
            {
                .name = "Tools",
                .description =
                "Tool names to remove. Example: [\"Service.Device.SetAudioPlayerVolume\","
                "\"Custom.Display.GetBrightness\"]",
                .type = service::FunctionValueType::Array,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> EXPLAIN_IMAGE_PARAMETERS = {{
            {
                .name = "Image",
                .description = "Image data.",
                .type = service::FunctionValueType::RawBuffer,
            },
            {
                .name = "Question",
                .description = "Question text.",
                .type = service::FunctionValueType::String,
                .default_value = DEFAULT_IMAGE_QUESTION,
            },
        }
    };

    inline static constexpr std::array<FunctionSpec, static_cast<size_t>(FunctionId::Max)> FUNCTION_SPECS = {{
            {
                .name = "AddMCP_ToolsWithServiceFunction",
                .description = "Add MCP tools from service functions.",
                .parameters = ADD_MCP_TOOLS_WITH_SERVICE_FUNCTION_PARAMETERS,
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::Array,
                    .description =
                    "Added tool names. Example: [\"Service.Device.SetAudioPlayerVolume\","
                    "\"Service.Device.GetAudioPlayerVolume\"]",
                },
            },
            {
                .name = "AddMCP_ToolsWithCustomFunction",
                .description = "Add custom MCP tools.",
                .parameters = ADD_MCP_TOOLS_WITH_CUSTOM_FUNCTION_PARAMETERS,
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::Array,
                    .description =
                    "Added tool names. Example: [\"Custom.Display.GetBrightness\","
                    "\"Custom.Display.SetBrightness\"]",
                },
            },
            {
                .name = "RemoveMCP_Tools",
                .description = "Remove MCP tools.",
                .parameters = REMOVE_MCP_TOOLS_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "ExplainImage",
                .description = "Explain an image.",
                .parameters = EXPLAIN_IMAGE_PARAMETERS,
                .return_value = FunctionReturnSpec{
                    .type = service::FunctionValueType::String,
                    .description = "Example: \"This image contains a cup on a desk.\"",
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

    inline static constexpr std::array<EventItemSpec, 1> ACTIVATION_CODE_RECEIVED_ITEMS = {{
            {"Code", "Activation code.", service::EventItemType::String},
        }
    };

    inline static constexpr std::array<EventSpec, static_cast<size_t>(EventId::Max)> EVENT_SPECS = {{
            {
                .name = "ActivationCodeReceived",
                .description = "Emitted when an activation code is received.",
                .items = ACTIVATION_CODE_RECEIVED_ITEMS,
            },
        }
    };
    static_assert(EVENT_SPECS.size() == static_cast<size_t>(EventId::Max));

public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the functions required by the Base class /////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Name of the XiaoZhi agent service.
     *
     * @return std::string_view Stable service name.
     */
    static constexpr std::string_view get_name()
    {
        return "XiaoZhi";
    }

    /**
     * @brief Get the function schemas exported by the XiaoZhi agent.
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
     * @brief Get the event schemas exported by the XiaoZhi agent.
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
/**
 * @brief  Function related
 */
BROOKESIA_DESCRIBE_ENUM(
    XiaoZhi::FunctionId, AddMCP_ToolsWithServiceFunction, AddMCP_ToolsWithCustomFunction, RemoveMCP_Tools,
    ExplainImage, Max
);

/**
 * @brief  Event related
 */
BROOKESIA_DESCRIBE_ENUM(XiaoZhi::EventId, ActivationCodeReceived, Max);

} // namespace esp_brookesia::service::helper
