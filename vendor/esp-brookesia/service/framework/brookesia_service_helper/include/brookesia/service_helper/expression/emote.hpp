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

namespace esp_brookesia::service::helper {

/**
 * @brief Helper schema definitions for the emote-expression service.
 */
class ExpressionEmote: public Base<ExpressionEmote> {
public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////// The following are the service specific types and enumerations ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Supported message categories rendered by the expression service.
     */
    enum class EventMessageType {
        Idle,
        Speak,
        Listen,
        System,
        User,
        Battery,
        Max,
    };

    /**
     * @brief Supported asset-source backends used when loading expression assets.
     */
    enum class AssetSourceType {
        Path,
        PartitionLabel,
        Max,
    };

    /**
     * @brief Description of one asset source used by the expression service.
     */
    struct AssetSource {
        std::string source; ///< Source identifier such as a path or partition label.
        AssetSourceType type; ///< How `source` should be interpreted.
        bool flag_enable_mmap = false; ///< Whether mmap-backed loading should be enabled.
    };

    /**
     * @brief Runtime display and task configuration for the expression service.
     */
    struct Config {
        uint32_t h_res = 0; ///< Horizontal resolution in pixels.
        uint32_t v_res = 0; ///< Vertical resolution in pixels.
        size_t buf_pixels = 0; ///< Display buffer size in pixels.
        uint32_t fps = 0; ///< Target render frame rate.
        int task_priority = 0; ///< Render task priority.
        int task_stack = 0; ///< Render task stack size in bytes.
        int task_affinity = 0; ///< Core affinity for the render task.
        bool task_stack_in_ext = false; ///< Whether the task stack should live in external memory.
        bool flag_swap_color_bytes = false; ///< Whether output color bytes must be swapped.
        bool flag_double_buffer = false; ///< Whether double buffering is enabled.
        bool flag_buff_dma = false; ///< Whether display buffers must be DMA-capable.
        bool flag_buff_spiram = false; ///< Whether display buffers may be allocated in SPIRAM.
    };

    /**
     * @brief Parameters delivered with the flush-ready event.
     */
    struct FlushReadyEventParam {
        int x_start = 0; ///< Left edge of the dirty region.
        int y_start = 0; ///< Top edge of the dirty region.
        int x_end = 0; ///< Right edge of the dirty region.
        int y_end = 0; ///< Bottom edge of the dirty region.
        const void *data = nullptr; ///< Pixel buffer for the dirty region.
    };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the types required by the Base class /////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    enum class FunctionId {
        SetConfig,
        LoadAssetsSource,
        SetEmoji,
        HideEmoji,
        SetAnimation,
        InsertAnimation,
        StopAnimation,
        WaitAnimationFrameDone,
        SetEventMessage,
        HideEventMessage,
        SetQrcode,
        HideQrcode,
        NotifyFlushFinished,
        RefreshAll,
        Max,
    };

    enum class EventId {
        FlushReady,
        Max,
    };










private:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the static schema specifications ////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    using DefaultValueKind = detail::static_schema::DefaultValueKind;
    using DefaultValueSpec = detail::static_schema::DefaultValueSpec;
    using EventItemSpec = detail::static_schema::EventItemSpec;
    using EventSpec = detail::static_schema::EventSpec;
    using FunctionParameterSpec = detail::static_schema::FunctionParameterSpec;
    using FunctionSpec = detail::static_schema::FunctionSpec;

    inline static constexpr std::span<const FunctionParameterSpec> EMPTY_PARAMETERS = {};

    inline static constexpr DefaultValueSpec ZERO_NUMBER_DEFAULT = {
        .kind = DefaultValueKind::Number,
        .number = 0,
    };

    inline static constexpr DefaultValueSpec EMPTY_STRING_DEFAULT = {
        .kind = DefaultValueKind::String,
        .string = "",
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> SET_CONFIG_PARAMETERS = {{
            {
                .name = "Config",
                .description =
                R"(Config. Example: {"h_res":320,"v_res":240,"buf_pixels":7680,"fps":30,)"
                R"("task_priority":5,"task_stack":4096,"task_affinity":0,"task_stack_in_ext":true,)"
                R"("flag_swap_color_bytes":false,"flag_double_buffer":false,"flag_buff_dma":false,)"
                R"("flag_buff_spiram":true})",
                .type = FunctionValueType::Object,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> LOAD_ASSETS_SOURCE_PARAMETERS = {{
            {
                .name = "Source",
                .description =
                R"(Asset source as a JSON object. Example: {"source":"anim_icon","type":"PartitionLabel",)"
                R"("flag_enable_mmap":false})",
                .type = FunctionValueType::Object,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> SET_EMOJI_PARAMETERS = {{
            {
                .name = "Emoji",
                .description = "Emoji name.",
                .type = FunctionValueType::String,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> SET_ANIMATION_PARAMETERS = {{
            {
                .name = "Animation",
                .description = "Animation name.",
                .type = FunctionValueType::String,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> INSERT_ANIMATION_PARAMETERS = {{
            {
                .name = "Animation",
                .description = "Animation name.",
                .type = FunctionValueType::String,
            },
            {
                .name = "DurationMs",
                .description = "Animation duration in milliseconds. Stops automatically after this duration.",
                .type = FunctionValueType::Number,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> WAIT_ANIMATION_FRAME_DONE_PARAMETERS = {{
            {
                .name = "TimeoutMs",
                .description = "Timeout in milliseconds. `0` means wait forever.",
                .type = FunctionValueType::Number,
                .default_value = ZERO_NUMBER_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> SET_EVENT_MESSAGE_PARAMETERS = {{
            {
                .name = "Event",
                .description = "Event type. Allowed values: [Idle, Speak, Listen, System, User, Battery]",
                .type = FunctionValueType::String,
            },
            {
                .name = "Message",
                .description =
                "Message text. For Battery event, the message format is \"<charging_status>,<percentage>\", "
                "where <charging_status> is `0` for not charging and `1` for charging, and <percentage> is "
                "the battery percentage in [0, 100].",
                .type = FunctionValueType::String,
                .default_value = EMPTY_STRING_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> SET_QRCODE_PARAMETERS = {{
            {
                .name = "Qrcode",
                .description = "QR code content.",
                .type = FunctionValueType::String,
            },
        }
    };

    inline static constexpr std::array<FunctionSpec, static_cast<size_t>(FunctionId::Max)> FUNCTION_SPECS = {{
            {
                .name = "SetConfig",
                .description = "Set emote config.",
                .parameters = SET_CONFIG_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "LoadAssetsSource",
                .description = "Load assets from the specified source.",
                .parameters = LOAD_ASSETS_SOURCE_PARAMETERS,
            },
            {
                .name = "SetEmoji",
                .description = "Set emoji and hide animation immediately.",
                .parameters = SET_EMOJI_PARAMETERS,
            },
            {
                .name = "HideEmoji",
                .description = "Hide current emoji.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "SetAnimation",
                .description = "Set animation and hide emoji immediately.",
                .parameters = SET_ANIMATION_PARAMETERS,
            },
            {
                .name = "InsertAnimation",
                .description = "Insert animation; it hides immediately and shows after the duration.",
                .parameters = INSERT_ANIMATION_PARAMETERS,
            },
            {
                .name = "StopAnimation",
                .description = "Stop current animation and hide it immediately.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "WaitAnimationFrameDone",
                .description = "Wait for each animation frame to finish.",
                .parameters = WAIT_ANIMATION_FRAME_DONE_PARAMETERS,
            },
            {
                .name = "SetEventMessage",
                .description = "Set message for a specified emote event.",
                .parameters = SET_EVENT_MESSAGE_PARAMETERS,
            },
            {
                .name = "HideEventMessage",
                .description = "Hide current event message.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "SetQrcode",
                .description = "Set QR code and hide emoji and animation immediately.",
                .parameters = SET_QRCODE_PARAMETERS,
            },
            {
                .name = "HideQrcode",
                .description = "Hide current QR code and show emoji immediately.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "NotifyFlushFinished",
                .description = "Notify emote flush finished.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "RefreshAll",
                .description = "Refresh the screen.",
                .parameters = EMPTY_PARAMETERS,
            },
        }
    };
    static_assert(FUNCTION_SPECS.size() == static_cast<size_t>(FunctionId::Max));

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the event schema specifications //////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline static constexpr std::array<EventItemSpec, 1> FLUSH_READY_ITEMS = {{
            {
                .name = "Param",
                .description =
                R"(Flush-ready parameter as a JSON object. Example: {"x_start":0,"y_start":0,"x_end":100,)"
                R"("y_end":100,"data":"@0x12345678"})",
                .type = EventItemType::Object,
            },
        }
    };

    inline static constexpr std::array<EventSpec, static_cast<size_t>(EventId::Max)> EVENT_SPECS = {{
            {
                .name = "FlushReady",
                .description = "Emitted when emote flush is ready.",
                .items = FLUSH_READY_ITEMS,
                .require_scheduler = false,
            },
        }
    };
    static_assert(EVENT_SPECS.size() == static_cast<size_t>(EventId::Max));

public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the functions required by the Base class /////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    static constexpr std::string_view get_name()
    {
        return "Emote";
    }

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
BROOKESIA_DESCRIBE_ENUM(ExpressionEmote::EventMessageType, Idle, Speak, Listen, System, User, Battery, Max);
BROOKESIA_DESCRIBE_ENUM(ExpressionEmote::AssetSourceType, Path, PartitionLabel, Max);
BROOKESIA_DESCRIBE_STRUCT(ExpressionEmote::AssetSource, (), (source, type, flag_enable_mmap));
BROOKESIA_DESCRIBE_STRUCT(
    ExpressionEmote::Config, (), (
        h_res, v_res, buf_pixels, fps, task_priority, task_stack, task_affinity, task_stack_in_ext,
        flag_swap_color_bytes, flag_double_buffer, flag_buff_dma, flag_buff_spiram
    )
);
BROOKESIA_DESCRIBE_STRUCT(ExpressionEmote::FlushReadyEventParam, (), (x_start, y_start, x_end, y_end, data));
BROOKESIA_DESCRIBE_ENUM(
    ExpressionEmote::FunctionId, SetConfig, LoadAssetsSource, SetEmoji, HideEmoji, SetAnimation, InsertAnimation,
    StopAnimation, WaitAnimationFrameDone, SetEventMessage, HideEventMessage, SetQrcode, HideQrcode,
    NotifyFlushFinished, RefreshAll, Max
);
BROOKESIA_DESCRIBE_ENUM(ExpressionEmote::EventId, FlushReady, Max);

} // namespace esp_brookesia::service::helper
