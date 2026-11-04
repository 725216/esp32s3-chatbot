/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/nes_impl.hpp"

namespace esp_brookesia::emulation {

std::expected<void, std::string> Nes::function_load(const boost::json::object &config_json)
{
    Config config;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(config_json, config)) {
        return std::unexpected("Failed to parse NES config");
    }
    return load(std::move(config));
}

std::expected<void, std::string> Nes::function_start()
{
    return start();
}

std::expected<void, std::string> Nes::function_pause()
{
    return pause();
}

std::expected<void, std::string> Nes::function_resume()
{
    return resume();
}

std::expected<void, std::string> Nes::function_stop()
{
    return stop();
}

std::expected<void, std::string> Nes::function_reset()
{
    return reset();
}

std::expected<void, std::string> Nes::function_save()
{
    return save();
}

std::expected<void, std::string> Nes::function_set_gamepad_state(const boost::json::object &state_json)
{
    GamepadState state;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(state_json, state)) {
        return std::unexpected("Failed to parse NES gamepad state");
    }
    return set_gamepad_state(state);
}

std::expected<std::string, std::string> Nes::function_get_state()
{
    return to_string(get_state());
}

std::vector<service::FunctionSchema> Nes::get_function_schemas()
{
    auto schemas = Helper::get_function_schemas();
    return std::vector<service::FunctionSchema>(schemas.begin(), schemas.end());
}

std::vector<service::EventSchema> Nes::get_event_schemas()
{
    auto schemas = Helper::get_event_schemas();
    return std::vector<service::EventSchema>(schemas.begin(), schemas.end());
}

service::ServiceBase::FunctionHandlerMap Nes::get_function_handlers()
{
    return {
        BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_1(
            Helper, Helper::FunctionId::Load, boost::json::object, function_load(PARAM)
        ),
        BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Start, function_start()),
        BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Pause, function_pause()),
        BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Resume, function_resume()),
        BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Stop, function_stop()),
        BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Reset, function_reset()),
        BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Save, function_save()),
        BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_1(
            Helper, Helper::FunctionId::SetGamepadState, boost::json::object, function_set_gamepad_state(PARAM)
        ),
        BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::GetState, function_get_state()),
    };
}

void Nes::set_state(State state)
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    publish_event(
        BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::StateChanged),
    {service::EventItem(to_string(state))}
    );
}

void Nes::publish_error(const std::string &message)
{
    publish_event(
        BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::Error),
    {service::EventItem(message)}
    );
}

#if BROOKESIA_EMULATION_NES_ENABLE_AUTO_REGISTER
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    service::ServiceBase, Nes, Nes::get_instance().get_attributes().name, Nes::get_instance(),
    BROOKESIA_EMULATION_NES_PLUGIN_SYMBOL
);
#endif

} // namespace esp_brookesia::emulation
