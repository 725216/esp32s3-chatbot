/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "brookesia/hal_interface/interfaces/display/panel.hpp"
#include "brookesia/hal_interface/interfaces/display/touch.hpp"
#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_manager/detail/static_schema.hpp"
#include "brookesia/service_manager/helper/base.hpp"

namespace esp_brookesia::service::helper {

class Display: public Base<Display> {
public:
    using PixelFormat = hal::display::PanelIface::PixelFormat;
    using TouchOperationMode = hal::display::TouchIface::OperationMode;

    enum class OutputSlot {
        HalPanel,
        Buffer,
    };

    struct OutputTouchCapability {
        uint32_t id = 0;
        std::string name;
        std::string instance;
        uint32_t max_points = 1;
        TouchOperationMode operation_mode = TouchOperationMode::Max;
    };

    struct OutputBacklightCapability {
        std::string instance;
        bool on_off_supported = false;
    };

    struct OutputInfo {
        uint32_t id = 0;
        std::string name;
        uint32_t width = 0;
        uint32_t height = 0;
        PixelFormat pixel_format = PixelFormat::RGB565;
        OutputSlot slot = OutputSlot::HalPanel;
        std::string panel_instance;
        std::string group_id;
        std::optional<OutputTouchCapability> touch = std::nullopt;
        std::optional<OutputBacklightCapability> backlight = std::nullopt;
    };

    struct SourceInfo {
        uint32_t id = 0;
        std::string name;
        std::string role;
        std::vector<std::string> preferred_outputs;
        int priority = 0;
    };

    struct TouchInfo {
        uint32_t id = 0;
        std::string name;
        uint32_t x_max = 0;
        uint32_t y_max = 0;
        uint32_t max_points = 1;
        TouchOperationMode operation_mode = TouchOperationMode::Max;
        std::string touch_instance;
        std::string group_id;
        std::vector<std::string> bound_outputs;
    };

    enum class TouchGestureEventType {
        Press,
        Pressing,
        Release,
    };

    enum class TouchGestureDirection {
        None,
        Up,
        Down,
        Left,
        Right,
    };

    enum class TouchGestureArea : uint8_t {
        Center     = 0,
        TopEdge    = (1 << 0),
        BottomEdge = (1 << 1),
        LeftEdge   = (1 << 2),
        RightEdge  = (1 << 3),
    };

    struct TouchGestureThreshold {
        uint8_t direction_angle = 45;
        uint16_t direction_vertical = 0;
        uint16_t direction_horizon = 0;
        uint16_t horizontal_edge = 0;
        uint16_t vertical_edge = 0;
        uint16_t duration_short_ms = 220;
        float speed_slow_px_per_ms = 0.6F;
    };

    struct TouchGestureConfig {
        bool enabled = false;
        uint16_t detect_period_ms = 20;
        bool direction_lock_enabled = true;
        uint16_t release_debounce_ms = 40;
        TouchGestureThreshold threshold;
    };

    struct TouchGestureInfo {
        uint32_t output_id = 0;
        std::string output_name;
        std::string touch_name;
        TouchGestureEventType event_type = TouchGestureEventType::Press;
        TouchGestureDirection direction = TouchGestureDirection::None;
        uint8_t start_area = static_cast<uint8_t>(TouchGestureArea::Center);
        uint8_t stop_area = static_cast<uint8_t>(TouchGestureArea::Center);
        int start_x = -1;
        int start_y = -1;
        int stop_x = -1;
        int stop_y = -1;
        uint32_t duration_ms = 0;
        float speed_px_per_ms = 0;
        float distance_px = 0;
        bool flags_slow_speed = false;
        bool flags_short_duration = false;
    };

    enum class SourceState {
        Registered,
        Requested,
        Granted,
        Dummy,
        Revoked,
    };

    enum class FunctionId {
        GetOutputs,
        GetSources,
        RegisterSource,
        UnregisterSource,
        RequestOutput,
        ReleaseOutput,
        SetActiveSource,
        GetActiveSource,
        SetActiveSourceRole,
        GetActiveRole,
        GetSourceRoles,
        SetTouchGestureConfig,
        GetTouchGestureConfig,
        SetBacklightBrightness,
        GetBacklightBrightness,
        SetBacklightOnOff,
        GetBacklightOnOff,
        LoadData,
        ResetData,
        Max,
    };

    enum class EventId {
        SourceStateChanged,
        ActiveSourceChanged,
        OutputRegistered,
        OutputUnregistered,
        TouchGesture,
        BacklightBrightnessChanged,
        BacklightOnOffChanged,
        Max,
    };
























    static constexpr std::string_view get_name()
    {
        return "Display";
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

private:
    using DefaultValueKind = detail::static_schema::DefaultValueKind;
    using DefaultValueSpec = detail::static_schema::DefaultValueSpec;
    using FunctionParameterSpec = detail::static_schema::FunctionParameterSpec;
    using FunctionReturnSpec = detail::static_schema::FunctionReturnSpec;
    using FunctionSpec = detail::static_schema::FunctionSpec;

    inline static constexpr DefaultValueSpec EMPTY_STRING_DEFAULT = {
        .kind = DefaultValueKind::String,
        .string = "",
    };

    inline static constexpr DefaultValueSpec ZERO_NUMBER_DEFAULT = {
        .kind = DefaultValueKind::Number,
        .number = 0,
    };

    inline static constexpr DefaultValueSpec TOUCH_GESTURE_CONFIG_DEFAULT = {
        .kind = DefaultValueKind::JsonObject,
        .string =
        R"({"enabled":false,"detect_period_ms":20,"direction_lock_enabled":true,"release_debounce_ms":40,)"
        R"("threshold":{"direction_vertical":0,"direction_horizon":0,"direction_angle":45,"horizontal_edge":0,)"
        R"("vertical_edge":0,"duration_short_ms":220,"speed_slow_px_per_ms":6.000000238418579E-1}})",
    };

    inline static constexpr char OUTPUT_ID_DESCRIPTION[] = "Runtime Display output id from GetOutputs().";
    inline static constexpr char OUTPUT_NAME_DESCRIPTION[] =
        "Display output name, for example Output0. Empty string uses the first output.";
    inline static constexpr char OUTPUT_NAME_OR_ALL_DESCRIPTION[] =
        "Runtime Display output id from GetOutputs(), or 0 for all outputs.";

    inline static constexpr std::array<FunctionParameterSpec, 1> REGISTER_SOURCE_PARAMETERS = {{
            {
                .name = "Source",
                .description = "Display source info object. The id field is assigned by Display service.",
                .type = FunctionValueType::Object,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> UNREGISTER_SOURCE_PARAMETERS = {{
            {
                .name = "SourceName",
                .description = "Display source name.",
                .type = FunctionValueType::String,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> REQUEST_OUTPUT_PARAMETERS = {{
            {
                .name = "SourceName",
                .description = "Registered source name.",
                .type = FunctionValueType::String,
            },
            {
                .name = "OutputName",
                .description = "Display output name. Empty string uses the first output.",
                .type = FunctionValueType::String,
                .default_value = EMPTY_STRING_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> RELEASE_OUTPUT_PARAMETERS = {{
            {
                .name = "SourceName",
                .description = "Registered source name.",
                .type = FunctionValueType::String,
            },
            {
                .name = "OutputName",
                .description = "Display output name. Empty string uses the first output.",
                .type = FunctionValueType::String,
                .default_value = EMPTY_STRING_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> SET_ACTIVE_SOURCE_PARAMETERS = {{
            {
                .name = "OutputName",
                .description = OUTPUT_NAME_DESCRIPTION,
                .type = FunctionValueType::String,
                .default_value = EMPTY_STRING_DEFAULT,
            },
            {
                .name = "SourceName",
                .description = "Registered source name, or empty string to clear.",
                .type = FunctionValueType::String,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> GET_ACTIVE_SOURCE_PARAMETERS = {{
            {
                .name = "OutputName",
                .description = OUTPUT_NAME_DESCRIPTION,
                .type = FunctionValueType::String,
                .default_value = EMPTY_STRING_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> SET_ACTIVE_SOURCE_ROLE_PARAMETERS = {{
            {
                .name = "OutputName",
                .description = OUTPUT_NAME_DESCRIPTION,
                .type = FunctionValueType::String,
                .default_value = EMPTY_STRING_DEFAULT,
            },
            {
                .name = "Role",
                .description = "Registered source role.",
                .type = FunctionValueType::String,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> GET_ACTIVE_ROLE_PARAMETERS = {{
            {
                .name = "OutputName",
                .description = OUTPUT_NAME_DESCRIPTION,
                .type = FunctionValueType::String,
                .default_value = EMPTY_STRING_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> SET_TOUCH_GESTURE_CONFIG_PARAMETERS = {{
            {
                .name = "OutputId",
                .description = OUTPUT_ID_DESCRIPTION,
                .type = FunctionValueType::Number,
            },
            {
                .name = "Config",
                .description = "Gesture configuration object. Zero threshold fields use output defaults.",
                .type = FunctionValueType::Object,
                .default_value = TOUCH_GESTURE_CONFIG_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> GET_TOUCH_GESTURE_CONFIG_PARAMETERS = {{
            {
                .name = "OutputId",
                .description = OUTPUT_ID_DESCRIPTION,
                .type = FunctionValueType::Number,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> SET_BACKLIGHT_BRIGHTNESS_PARAMETERS = {{
            {
                .name = "OutputId",
                .description = OUTPUT_ID_DESCRIPTION,
                .type = FunctionValueType::Number,
            },
            {
                .name = "Brightness",
                .description = "Backlight brightness percentage in range [0, 100].",
                .type = FunctionValueType::Number,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> GET_BACKLIGHT_BRIGHTNESS_PARAMETERS = {{
            {
                .name = "OutputId",
                .description = OUTPUT_ID_DESCRIPTION,
                .type = FunctionValueType::Number,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 2> SET_BACKLIGHT_ON_OFF_PARAMETERS = {{
            {
                .name = "OutputId",
                .description = OUTPUT_ID_DESCRIPTION,
                .type = FunctionValueType::Number,
            },
            {
                .name = "On",
                .description = "True to turn on the backlight, false to turn it off.",
                .type = FunctionValueType::Boolean,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> GET_BACKLIGHT_ON_OFF_PARAMETERS = {{
            {
                .name = "OutputId",
                .description = OUTPUT_ID_DESCRIPTION,
                .type = FunctionValueType::Number,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> LOAD_DATA_PARAMETERS = {{
            {
                .name = "OutputId",
                .description = OUTPUT_NAME_OR_ALL_DESCRIPTION,
                .type = FunctionValueType::Number,
                .default_value = ZERO_NUMBER_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> RESET_DATA_PARAMETERS = {{
            {
                .name = "OutputId",
                .description = OUTPUT_NAME_OR_ALL_DESCRIPTION,
                .type = FunctionValueType::Number,
                .default_value = ZERO_NUMBER_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionSpec, static_cast<size_t>(FunctionId::Max)> FUNCTION_SPECS = {{
            {
                .name = "GetOutputs",
                .description = "Get display outputs.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Array,
                    .description =
                    R"(Example: [{"id":1,"name":"Output0","width":240,"height":240,"pixel_format":"RGB565",)"
                    R"("slot":"HalPanel","panel_instance":"Display:Panel0","group_id":"main_display","touch":)"
                    R"({"id":1,"name":"Touch0","instance":"Display:Touch0","max_points":5,)"
                    R"("operation_mode":"Interrupt"},"backlight":{"instance":"Display:Backlight0",)"
                    R"("on_off_supported":true}}])",
                },
            },
            {
                .name = "GetSources",
                .description = "Get registered display sources.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Array,
                    .description = R"(Example: [{"id":1,"name":"LVGL","role":"gui",)"
                    R"("preferred_outputs":["Output0"],"priority":0}])",
                },
            },
            {
                .name = "RegisterSource",
                .description =
                R"(Register a display source. Example source: {"id":0,"name":"LVGL","role":"gui",)"
                R"("preferred_outputs":["Output0"],"priority":0})",
                .parameters = REGISTER_SOURCE_PARAMETERS,
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Number,
                    .description = "Registered display source id.",
                },
            },
            {
                .name = "UnregisterSource",
                .description = "Unregister a named display source.",
                .parameters = UNREGISTER_SOURCE_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "RequestOutput",
                .description = "Request permission for a source to draw to one output.",
                .parameters = REQUEST_OUTPUT_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "ReleaseOutput",
                .description = "Release a source's request for one output.",
                .parameters = RELEASE_OUTPUT_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "SetActiveSource",
                .description = "Grant one display output to a named source. Empty source name clears the active source.",
                .parameters = SET_ACTIVE_SOURCE_PARAMETERS,
            },
            {
                .name = "GetActiveSource",
                .description =
                "Get the active source name for one display output. Returns empty string when no source is active.",
                .parameters = GET_ACTIVE_SOURCE_PARAMETERS,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::String,
                    .description = "Active source name, or an empty string when no source is active.",
                },
            },
            {
                .name = "SetActiveSourceRole",
                .description = "Grant one display output to the first registered source with the specified role.",
                .parameters = SET_ACTIVE_SOURCE_ROLE_PARAMETERS,
            },
            {
                .name = "GetActiveRole",
                .description =
                "Get the active source role for one display output. Returns empty string when no source is active.",
                .parameters = GET_ACTIVE_ROLE_PARAMETERS,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::String,
                    .description = "Active source role, or an empty string when no source is active.",
                },
            },
            {
                .name = "GetSourceRoles",
                .description = "Get registered display source roles, de-duplicated in first-registration order.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Array,
                    .description = R"(Example: ["gui","video"])",
                },
            },
            {
                .name = "SetTouchGestureConfig",
                .description =
                R"(Configure touch gesture detection for one output. Example config: {"enabled":false,)"
                R"("detect_period_ms":20,"direction_lock_enabled":true,"release_debounce_ms":40,"threshold":)"
                R"({"direction_vertical":0,"direction_horizon":0,"direction_angle":45,"horizontal_edge":0,)"
                R"("vertical_edge":0,"duration_short_ms":220,"speed_slow_px_per_ms":6.000000238418579E-1}})",
                .parameters = SET_TOUCH_GESTURE_CONFIG_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "GetTouchGestureConfig",
                .description = "Get touch gesture config for one output.",
                .parameters = GET_TOUCH_GESTURE_CONFIG_PARAMETERS,
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Object,
                    .description =
                    R"(Example: {"enabled":false,"detect_period_ms":20,"direction_lock_enabled":true,)"
                    R"("release_debounce_ms":40,"threshold":{"direction_vertical":0,"direction_horizon":0,)"
                    R"("direction_angle":45,"horizontal_edge":0,"vertical_edge":0,"duration_short_ms":220,)"
                    R"("speed_slow_px_per_ms":6.000000238418579E-1}})",
                },
            },
            {
                .name = "SetBacklightBrightness",
                .description = "Set backlight brightness percentage for one display output.",
                .parameters = SET_BACKLIGHT_BRIGHTNESS_PARAMETERS,
            },
            {
                .name = "GetBacklightBrightness",
                .description = "Get backlight brightness percentage for one display output.",
                .parameters = GET_BACKLIGHT_BRIGHTNESS_PARAMETERS,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Number,
                    .description = "Backlight brightness percentage in range [0, 100].",
                },
            },
            {
                .name = "SetBacklightOnOff",
                .description = "Turn the backlight on or off for one display output.",
                .parameters = SET_BACKLIGHT_ON_OFF_PARAMETERS,
            },
            {
                .name = "GetBacklightOnOff",
                .description = "Get whether the backlight is enabled for one display output.",
                .parameters = GET_BACKLIGHT_ON_OFF_PARAMETERS,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Boolean,
                    .description = "True when backlight is enabled, false otherwise.",
                },
            },
            {
                .name = "LoadData",
                .description = "Load persisted Display state. OutputId 0 loads all backlight-bound outputs.",
                .parameters = LOAD_DATA_PARAMETERS,
            },
            {
                .name = "ResetData",
                .description = "Reset persisted Display state. OutputId 0 resets all backlight-bound outputs.",
                .parameters = RESET_DATA_PARAMETERS,
            },
        }
    };
    static_assert(FUNCTION_SPECS.size() == static_cast<size_t>(FunctionId::Max));

    using EventItemSpec = detail::static_schema::EventItemSpec;
    using EventSpec = detail::static_schema::EventSpec;

    inline static constexpr std::array<EventItemSpec, 3> SOURCE_STATE_CHANGED_ITEMS = {{
            {"SourceName", "Source name.", EventItemType::String},
            {"OutputName", "Output name, or empty string for source-global changes.", EventItemType::String},
            {"State", "New source state.", EventItemType::String},
        }
    };

    inline static constexpr std::array<EventItemSpec, 2> ACTIVE_SOURCE_CHANGED_ITEMS = {{
            {"OutputName", "Output name.", EventItemType::String},
            {"SourceName", "Active source name, or empty string when cleared.", EventItemType::String},
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> OUTPUT_REGISTERED_ITEMS = {{
            {"Info", "Registered output info.", EventItemType::Object},
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> OUTPUT_UNREGISTERED_ITEMS = {{
            {"OutputName", "Display output name.", EventItemType::String},
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> TOUCH_GESTURE_ITEMS = {{
            {"Info", "Gesture event payload.", EventItemType::Object},
        }
    };

    inline static constexpr std::array<EventItemSpec, 3> BACKLIGHT_BRIGHTNESS_CHANGED_ITEMS = {{
            {"OutputId", "Runtime Display output id.", EventItemType::Number},
            {"OutputName", "Output name.", EventItemType::String},
            {"Brightness", "Current target backlight brightness percentage [0, 100].", EventItemType::Number},
        }
    };

    inline static constexpr std::array<EventItemSpec, 3> BACKLIGHT_ON_OFF_CHANGED_ITEMS = {{
            {"OutputId", "Runtime Display output id.", EventItemType::Number},
            {"OutputName", "Output name.", EventItemType::String},
            {"IsOn", "Whether display backlight is currently on. True if on, false if off.", EventItemType::Boolean},
        }
    };

    inline static constexpr std::array<EventSpec, BROOKESIA_DESCRIBE_ENUM_TO_NUM(EventId::Max)> EVENT_SPECS = {{
            {
                .name = "SourceStateChanged",
                .description = "A display source state changed relative to an output.",
                .items = SOURCE_STATE_CHANGED_ITEMS,
                .require_scheduler = false,
            },
            {
                .name = "ActiveSourceChanged",
                .description = "The active source for an output changed.",
                .items = ACTIVE_SOURCE_CHANGED_ITEMS,
                .require_scheduler = false,
            },
            {
                .name = "OutputRegistered",
                .description = "A dynamic display output was registered.",
                .items = OUTPUT_REGISTERED_ITEMS,
                .require_scheduler = false,
            },
            {
                .name = "OutputUnregistered",
                .description = "A dynamic display output was unregistered.",
                .items = OUTPUT_UNREGISTERED_ITEMS,
                .require_scheduler = false,
            },
            {
                .name = "TouchGesture",
                .description = "A display output touch gesture changed state.",
                .items = TOUCH_GESTURE_ITEMS,
                .require_scheduler = false,
            },
            {
                .name = "BacklightBrightnessChanged",
                .description = "The backlight brightness for an output changed.",
                .items = BACKLIGHT_BRIGHTNESS_CHANGED_ITEMS,
            },
            {
                .name = "BacklightOnOffChanged",
                .description = "The backlight on/off state for an output changed.",
                .items = BACKLIGHT_ON_OFF_CHANGED_ITEMS,
            },
        }
    };
    static_assert(EVENT_SPECS.size() == BROOKESIA_DESCRIBE_ENUM_TO_NUM(EventId::Max));
};

BROOKESIA_DESCRIBE_ENUM(
    Display::FunctionId, GetOutputs, GetSources, RegisterSource, UnregisterSource, RequestOutput, ReleaseOutput,
    SetActiveSource, GetActiveSource, SetActiveSourceRole, GetActiveRole, GetSourceRoles, SetTouchGestureConfig,
    GetTouchGestureConfig, SetBacklightBrightness, GetBacklightBrightness, SetBacklightOnOff, GetBacklightOnOff,
    LoadData, ResetData, Max
);
BROOKESIA_DESCRIBE_ENUM(
    Display::EventId, SourceStateChanged, ActiveSourceChanged, OutputRegistered, OutputUnregistered, TouchGesture,
    BacklightBrightnessChanged, BacklightOnOffChanged, Max
);
BROOKESIA_DESCRIBE_ENUM(Display::OutputSlot, HalPanel, Buffer);
BROOKESIA_DESCRIBE_ENUM(Display::SourceState, Registered, Requested, Granted, Dummy, Revoked);
BROOKESIA_DESCRIBE_ENUM(Display::TouchGestureEventType, Press, Pressing, Release);
BROOKESIA_DESCRIBE_ENUM(Display::TouchGestureDirection, None, Up, Down, Left, Right);
BROOKESIA_DESCRIBE_ENUM(Display::TouchGestureArea, Center, TopEdge, BottomEdge, LeftEdge, RightEdge);
BROOKESIA_DESCRIBE_STRUCT(Display::OutputTouchCapability, (), (id, name, instance, max_points, operation_mode));
BROOKESIA_DESCRIBE_STRUCT(Display::OutputBacklightCapability, (), (instance, on_off_supported));
BROOKESIA_DESCRIBE_STRUCT(
    Display::OutputInfo, (),
    (id, name, width, height, pixel_format, slot, panel_instance, group_id, touch, backlight)
);
BROOKESIA_DESCRIBE_STRUCT(Display::SourceInfo, (), (id, name, role, preferred_outputs, priority));
BROOKESIA_DESCRIBE_STRUCT(
    Display::TouchInfo, (),
    (id, name, x_max, y_max, max_points, operation_mode, touch_instance, group_id, bound_outputs)
);
BROOKESIA_DESCRIBE_STRUCT(
    Display::TouchGestureThreshold, (),
    (
        direction_vertical, direction_horizon, direction_angle, horizontal_edge, vertical_edge, duration_short_ms,
        speed_slow_px_per_ms
    )
);
BROOKESIA_DESCRIBE_STRUCT(
    Display::TouchGestureConfig, (),
    (enabled, detect_period_ms, direction_lock_enabled, release_debounce_ms, threshold)
);
BROOKESIA_DESCRIBE_STRUCT(
    Display::TouchGestureInfo, (),
    (
        output_id, output_name, touch_name, event_type, direction, start_area, stop_area, start_x, start_y, stop_x, stop_y,
        duration_ms, speed_px_per_ms, distance_px, flags_slow_speed, flags_short_duration
    )
);

} // namespace esp_brookesia::service::helper
