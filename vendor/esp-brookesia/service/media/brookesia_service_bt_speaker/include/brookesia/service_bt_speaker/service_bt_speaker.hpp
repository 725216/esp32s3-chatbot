/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "brookesia/service_bt_speaker/macro_configs.h"
#include "brookesia/service_helper/media/bt_speaker.hpp"
#include "brookesia/service_helper/network/bt.hpp"
#include "brookesia/service_manager/service/base.hpp"

namespace esp_brookesia::service {

/** High-level A2DP speaker policy and control service. */
class BtSpeaker: public ServiceBase {
public:
    static BtSpeaker &get_instance()
    {
        static BtSpeaker instance;
        return instance;
    }

private:
    class CallbackContext;
    class Impl;

    using Helper = helper::BtSpeaker;
    using BtHelper = helper::Bt;
    using Config = Helper::Config;
    using State = Helper::State;
    using GeneralState = Helper::GeneralState;

    BtSpeaker();
    ~BtSpeaker() override;

    static std::string get_component_version();

    bool on_init() override;
    void on_deinit() override;
    bool on_start() override;
    void on_stop() override;

    std::expected<void, std::string> function_set_config(const boost::json::object &config);
    std::expected<boost::json::object, std::string> function_get_config();
    std::expected<void, std::string> function_start();
    std::expected<void, std::string> function_stop();
    std::expected<boost::json::object, std::string> function_get_state();
    std::expected<void, std::string> function_pause();
    std::expected<void, std::string> function_resume();
    std::expected<void, std::string> function_next();
    std::expected<void, std::string> function_previous();
    std::expected<void, std::string> function_set_volume(double volume);
    std::expected<double, std::string> function_get_volume();
    std::expected<void, std::string> function_disconnect();

    bool refresh_bt_event_subscriptions();
    bool subscribe_bt_events(uint64_t generation, const std::shared_ptr<CallbackContext> &context);
    void post_bt_event(
        const std::weak_ptr<CallbackContext> &context, uint64_t generation,
        std::function<void()> callback
    );
    void handle_bt_state_changed(const boost::json::object &state_json);
    void handle_profile_availability_changed(const std::string &profile, bool available);
    void handle_connection_state_changed(const std::string &state, const boost::json::object &connection_json);
    void handle_stream_state_changed(const std::string &state);
    void handle_playback_status_changed(const std::string &status);
    void handle_metadata_changed(const boost::json::object &metadata_json);
    void handle_volume_changed(double volume);
    void handle_bt_error(const std::string &operation, double code, const std::string &message);
    void stop_local_playback_async();
    void publish_state_changed();
    void publish_error(const std::string &operation, int code, const std::string &message);

    std::vector<FunctionSchema> get_function_schemas() override
    {
        const auto schemas = Helper::get_function_schemas();
        return {schemas.begin(), schemas.end()};
    }

    std::vector<EventSchema> get_event_schemas() override
    {
        const auto schemas = Helper::get_event_schemas();
        return {schemas.begin(), schemas.end()};
    }

    FunctionHandlerMap get_function_handlers() override
    {
        return {
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_1(
                Helper, Helper::FunctionId::SetConfig, boost::json::object, function_set_config(PARAM)
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(
                Helper, Helper::FunctionId::GetConfig, function_get_config()
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Start, function_start()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Stop, function_stop()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::GetState, function_get_state()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Pause, function_pause()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Resume, function_resume()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Next, function_next()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Previous, function_previous()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_1(
                Helper, Helper::FunctionId::SetVolume, double, function_set_volume(PARAM)
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(
                Helper, Helper::FunctionId::GetVolume, function_get_volume()
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(
                Helper, Helper::FunctionId::Disconnect, function_disconnect()
            ),
        };
    }

    std::shared_ptr<CallbackContext> callback_context_;
    std::unique_ptr<Impl> impl_;
};

} // namespace esp_brookesia::service
