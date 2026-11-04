/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <atomic>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "brookesia/hal_interface/interfaces/bluetooth/a2dp_sink.hpp"
#include "brookesia/hal_interface/interfaces/bluetooth/ble/peripheral.hpp"
#include "brookesia/service_bt/macro_configs.h"
#include "brookesia/service_helper/network/bt.hpp"
#include "brookesia/service_manager/dataflow/audio/playback_operation.hpp"
#include "brookesia/service_manager/service/base.hpp"

namespace esp_brookesia::service::bt {

/** Generic Bluetooth service with an A2DP Sink profile. */
class Bt: public ServiceBase {
public:
    static Bt &get_instance()
    {
        static Bt instance;
        return instance;
    }

private:
    using Helper = helper::Bt;
    using Profile = Helper::Profile;
    using HostState = Helper::HostState;
    using ConnectionState = Helper::ConnectionState;
    using StreamState = Helper::StreamState;
    using PlaybackStatus = Helper::PlaybackStatus;
    using DeviceConfig = Helper::DeviceConfig;
    using PeerInfo = Helper::PeerInfo;
    using TrackMetadata = Helper::TrackMetadata;
    using PcmFrame = hal::bluetooth::PcmFrame;

    Bt()
        : ServiceBase({
        .name = Helper::get_name().data(),
        .description = "Manage Bluetooth host profiles and A2DP Sink playback.",
        .version = get_component_version(),
    })
    {
    }
    ~Bt() override = default;

    static std::string get_component_version();

    bool on_init() override;
    void on_deinit() override;
    bool on_start() override;
    void on_stop() override;

    std::expected<boost::json::object, std::string> function_get_capabilities();
    std::expected<void, std::string> function_set_device_config(const boost::json::object &config);
    std::expected<boost::json::object, std::string> function_get_device_config();
    std::expected<void, std::string> function_start();
    std::expected<void, std::string> function_stop();
    std::expected<boost::json::object, std::string> function_get_state();
    std::expected<boost::json::array, std::string> function_list_profiles();
    std::expected<boost::json::array, std::string> function_get_connections();
    std::expected<void, std::string> function_disconnect(double connection_id);
    std::expected<void, std::string> function_a2dp_sink_start();
    std::expected<void, std::string> function_a2dp_sink_stop();
    std::expected<void, std::string> function_a2dp_pause();
    std::expected<void, std::string> function_a2dp_resume();
    std::expected<void, std::string> function_a2dp_next();
    std::expected<void, std::string> function_a2dp_previous();
    std::expected<void, std::string> function_a2dp_set_volume(double volume);
    std::expected<double, std::string> function_a2dp_get_volume();

    std::vector<FunctionSchema> get_function_schemas() override
    {
        auto schemas = Helper::get_function_schemas();
        return std::vector<FunctionSchema>(schemas.begin(), schemas.end());
    }

    std::vector<EventSchema> get_event_schemas() override
    {
        auto schemas = Helper::get_event_schemas();
        return std::vector<EventSchema>(schemas.begin(), schemas.end());
    }

    FunctionHandlerMap get_function_handlers() override
    {
        return {
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::GetCapabilities, function_get_capabilities()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_1(
                Helper, Helper::FunctionId::SetDeviceConfig, boost::json::object,
                function_set_device_config(PARAM)
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::GetDeviceConfig, function_get_device_config()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Start, function_start()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::Stop, function_stop()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::GetState, function_get_state()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::ListProfiles, function_list_profiles()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::GetConnections, function_get_connections()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_1(
                Helper, Helper::FunctionId::Disconnect, double, function_disconnect(PARAM)
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::A2dpSinkStart, function_a2dp_sink_start()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::A2dpSinkStop, function_a2dp_sink_stop()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::A2dpPause, function_a2dp_pause()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::A2dpResume, function_a2dp_resume()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::A2dpNext, function_a2dp_next()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::A2dpPrevious, function_a2dp_previous()),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_1(
                Helper, Helper::FunctionId::A2dpSetVolume, double, function_a2dp_set_volume(PARAM)
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(Helper, Helper::FunctionId::A2dpGetVolume, function_a2dp_get_volume()),
        };
    }

    bool start_a2dp();
    bool stop_a2dp(bool publish_state);
    void post_hal_callback(uint64_t generation, std::function<void()> callback);
    hal::bluetooth::A2dpSinkIface::Callbacks make_hal_callbacks(uint64_t generation);
    bool ensure_playback_operation(const hal::bluetooth::PcmFormat &format);
    void close_playback_operation();
    void handle_pcm(PcmFrame frame);
    void set_host_state(HostState state, bool publish = true);
    void publish_error(const std::string &operation, int code, const std::string &message);

    hal::InterfaceHandle<hal::bluetooth::A2dpSinkIface> a2dp_iface_;
    hal::InterfaceHandle<hal::bluetooth::ble::PeripheralIface> ble_iface_;
    mutable std::mutex mutex_;
    DeviceConfig device_config_{.device_name = "ESP-Brookesia-Bt"};
    Helper::Capabilities capabilities_;
    HostState host_state_ = HostState::Idle;
    ConnectionState connection_state_ = ConnectionState::Disconnected;
    StreamState stream_state_ = StreamState::Idle;
    PlaybackStatus playback_status_ = PlaybackStatus::Unknown;
    std::optional<PeerInfo> connection_;
    TrackMetadata metadata_;
    uint8_t volume_ = 100;
    bool hal_configured_ = false;
    bool hal_initialized_ = false;
    bool hal_started_ = false;
    std::shared_ptr<dataflow::AudioPlaybackOperation> playback_operation_;
    std::string playback_output_name_;
    std::atomic<uint64_t> callback_generation_{0};
};

} // namespace esp_brookesia::service::bt
