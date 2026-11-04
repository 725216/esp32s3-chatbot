/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_manager/detail/static_schema.hpp"
#include "brookesia/service_manager/helper/base.hpp"

namespace esp_brookesia::service::helper {

class Nes: public Base<Nes> {
public:
    enum class State {
        Idle,
        Loaded,
        Running,
        Paused,
        Stopped,
        Error,
    };

    enum class VideoMode {
        Native,
        Fit,
        Fill,
    };

    enum class AudioMode {
        Disabled,
        Auto,
        Required,
    };

    struct GamepadState {
        bool up = false;
        bool down = false;
        bool left = false;
        bool right = false;
        bool a = false;
        bool b = false;
        bool select = false;
        bool start = false;
        bool x = false;
    };

    struct VideoArea {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct Config {
        std::string rom_path;
        std::string save_path;
        std::string display_output_name;
        std::string display_source_name = "NES";
        VideoArea video_area;
        VideoMode video_mode = VideoMode::Fit;
        AudioMode audio_mode = AudioMode::Auto;
        bool auto_activate_display = false;
    };

    enum class FunctionId {
        Load,
        Start,
        Pause,
        Resume,
        Stop,
        Reset,
        Save,
        SetGamepadState,
        GetState,
        Max,
    };

    enum class EventId {
        StateChanged,
        Error,
        SaveCompleted,
        Max,
    };






    static constexpr std::string_view get_name()
    {
        return "NES";
    }

private:
    using EventItemSpec = detail::static_schema::EventItemSpec;
    using EventSpec = detail::static_schema::EventSpec;
    using FunctionParameterSpec = detail::static_schema::FunctionParameterSpec;
    using FunctionReturnSpec = detail::static_schema::FunctionReturnSpec;
    using FunctionSpec = detail::static_schema::FunctionSpec;

    inline static constexpr std::span<const FunctionParameterSpec> EMPTY_PARAMETERS = {};

    inline static constexpr std::array<FunctionParameterSpec, 1> LOAD_PARAMETERS = {{
            {
                .name = "Config",
                .description = "NES runtime configuration. video_area uses the full output when width/height are 0.",
                .type = FunctionValueType::Object,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> GAMEPAD_STATE_PARAMETERS = {{
            {
                .name = "State",
                .description = "Gamepad state object.",
                .type = FunctionValueType::Object,
            },
        }
    };

    inline static constexpr std::array<FunctionSpec, static_cast<std::size_t>(FunctionId::Max)> FUNCTION_SPECS = {{
            {
                .name = "Load",
                .description =
                R"(Load a NES ROM. Example config: {"rom_path":"/sdcard/roms/demo.nes","save_path":"/sdcard/roms/demo_nes.save","display_output_name":"Output0","display_source_name":"NES","video_area":{"x":0,"y":0,"width":0,"height":0},"video_mode":"Fit","audio_mode":"Auto","auto_activate_display":false})",
                .parameters = LOAD_PARAMETERS,
            },
            {
                .name = "Start",
                .description = "Start or continue the loaded NES runtime.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "Pause",
                .description = "Pause the NES runtime.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "Resume",
                .description = "Resume the NES runtime.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "Stop",
                .description = "Stop the NES runtime.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "Reset",
                .description = "Soft reset the loaded NES runtime.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "Save",
                .description = "Save SRAM to the configured save path.",
                .parameters = EMPTY_PARAMETERS,
            },
            {
                .name = "SetGamepadState",
                .description =
                R"(Set current NES gamepad state. Example: {"up":false,"down":false,"left":false,"right":false,"a":false,"b":false,"select":false,"start":false,"x":false})",
                .parameters = GAMEPAD_STATE_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "GetState",
                .description = "Get NES runtime state.",
                .parameters = EMPTY_PARAMETERS,
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::String,
                    .description = R"(Example: "Running")",
                },
            },
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> STATE_CHANGED_ITEMS = {{
            {
                .name = "State",
                .description = "Runtime state.",
                .type = EventItemType::String,
            },
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> ERROR_ITEMS = {{
            {
                .name = "Message",
                .description = "Error message.",
                .type = EventItemType::String,
            },
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> SAVE_COMPLETED_ITEMS = {{
            {
                .name = "SavePath",
                .description = "Save path.",
                .type = EventItemType::String,
            },
        }
    };

    inline static constexpr std::array<EventSpec, static_cast<std::size_t>(EventId::Max)> EVENT_SPECS = {{
            {
                .name = "StateChanged",
                .description = "NES runtime state changed.",
                .items = STATE_CHANGED_ITEMS,
            },
            {
                .name = "Error",
                .description = "NES runtime error.",
                .items = ERROR_ITEMS,
            },
            {
                .name = "SaveCompleted",
                .description = "NES SRAM save completed.",
                .items = SAVE_COMPLETED_ITEMS,
            },
        }
    };

    static_assert(FUNCTION_SPECS.size() == static_cast<std::size_t>(FunctionId::Max));
    static_assert(EVENT_SPECS.size() == static_cast<std::size_t>(EventId::Max));

public:
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

BROOKESIA_DESCRIBE_ENUM(Nes::State, Idle, Loaded, Running, Paused, Stopped, Error);
BROOKESIA_DESCRIBE_ENUM(Nes::VideoMode, Native, Fit, Fill);
BROOKESIA_DESCRIBE_ENUM(Nes::AudioMode, Disabled, Auto, Required);
BROOKESIA_DESCRIBE_STRUCT(
    Nes::GamepadState, (), (up, down, left, right, a, b, select, start, x)
);
BROOKESIA_DESCRIBE_STRUCT(
    Nes::VideoArea, (), (x, y, width, height)
);
BROOKESIA_DESCRIBE_STRUCT(
    Nes::Config, (),
    (
        rom_path, save_path, display_output_name, display_source_name, video_area, video_mode, audio_mode,
        auto_activate_display
    )
);
BROOKESIA_DESCRIBE_ENUM(
    Nes::FunctionId, Load, Start, Pause, Resume, Stop, Reset, Save, SetGamepadState, GetState, Max
);
BROOKESIA_DESCRIBE_ENUM(Nes::EventId, StateChanged, Error, SaveCompleted, Max);

} // namespace esp_brookesia::service::helper
