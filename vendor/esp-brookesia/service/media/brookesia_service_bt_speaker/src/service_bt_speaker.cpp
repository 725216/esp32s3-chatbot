/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

#include "brookesia/service_bt_speaker/macro_configs.h"
#if !BROOKESIA_SERVICE_BT_SPEAKER_SERVICE_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "brookesia/lib_utils/plugin.hpp"
#include "brookesia/service_bt_speaker.hpp"
#include "brookesia/service_helper/media/audio.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::service {

class BtSpeaker::CallbackContext {
public:
    std::recursive_mutex gate;
    std::atomic<uint64_t> generation{0};
    std::atomic_bool active{false};
};

class BtSpeaker::Impl {
public:
    std::mutex mutex;
    std::optional<Config> config;
    State state;
    std::vector<EventRegistry::SignalConnection> bt_connections;
    bool profile_requested = false;
    bool owns_profile = false;
};

namespace {

bool is_music_active(helper::BtSpeaker::StreamState stream_state, helper::BtSpeaker::PlaybackStatus status)
{
    return stream_state == helper::BtSpeaker::StreamState::Started &&
           status != helper::BtSpeaker::PlaybackStatus::Paused &&
           status != helper::BtSpeaker::PlaybackStatus::Stopped;
}

bool is_valid_volume(double volume)
{
    return std::isfinite(volume) && volume >= 0 && volume <= 100 && std::floor(volume) == volume;
}

bool has_a2dp_sink(const helper::Bt::Capabilities &capabilities)
{
    return capabilities.classic_supported &&
           std::find(capabilities.profiles.begin(), capabilities.profiles.end(), helper::Bt::Profile::A2dpSink) !=
           capabilities.profiles.end();
}

void reset_runtime_state(helper::BtSpeaker::State &state)
{
    state.is_started = false;
    state.is_connected = false;
    state.is_music_active = false;
    state.connection_state = helper::BtSpeaker::ConnectionState::Disconnected;
    state.stream_state = helper::BtSpeaker::StreamState::Idle;
    state.playback_status = helper::BtSpeaker::PlaybackStatus::Unknown;
    state.volume = 0;
    state.connection.reset();
    state.metadata = {};
}

} // namespace

BtSpeaker::BtSpeaker()
    : ServiceBase({
    .name = Helper::get_name().data(),
    .description = "Manage Bluetooth speaker playback and media policy.",
    .version = get_component_version(),
    .dependencies = {BtHelper::get_name().data()},
})
{
}

BtSpeaker::~BtSpeaker() = default;

std::string BtSpeaker::get_component_version()
{
    return make_version(
               BROOKESIA_SERVICE_BT_SPEAKER_VER_MAJOR,
               BROOKESIA_SERVICE_BT_SPEAKER_VER_MINOR,
               BROOKESIA_SERVICE_BT_SPEAKER_VER_PATCH
           );
}

bool BtSpeaker::on_init()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_LOGI(
        "Version: %1%.%2%.%3%", BROOKESIA_SERVICE_BT_SPEAKER_VER_MAJOR,
        BROOKESIA_SERVICE_BT_SPEAKER_VER_MINOR, BROOKESIA_SERVICE_BT_SPEAKER_VER_PATCH
    );
    callback_context_ = std::make_shared<CallbackContext>();
    impl_ = std::make_unique<Impl>();
    return true;
}

void BtSpeaker::on_deinit()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    auto context = callback_context_;
    if (context) {
        context->active.store(false);
        context->generation.fetch_add(1);
    }
    if (context) {
        std::lock_guard gate(context->gate);
        if (impl_) {
            impl_->bt_connections.clear();
        }
        impl_.reset();
    } else {
        impl_.reset();
    }
    callback_context_.reset();
}

bool BtSpeaker::on_start()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
    BROOKESIA_CHECK_NULL_RETURN(impl_, false, "Bluetooth speaker implementation is not initialized");
    BROOKESIA_CHECK_NULL_RETURN(callback_context_, false, "Bluetooth speaker callback context is not initialized");

    callback_context_->active.store(true);
    std::lock_guard gate(callback_context_->gate);
    if (!refresh_bt_event_subscriptions()) {
        callback_context_->active.store(false);
        BROOKESIA_LOGE("Failed to subscribe to required Bt service events");
        return false;
    }

    auto capabilities_result = BtHelper::call_function_sync<boost::json::object>(
                                   BtHelper::FunctionId::GetCapabilities
                               );
    if (capabilities_result) {
        BtHelper::Capabilities capabilities;
        if (BROOKESIA_DESCRIBE_FROM_JSON(capabilities_result.value(), capabilities)) {
            std::lock_guard lock(impl_->mutex);
            impl_->state.is_supported = has_a2dp_sink(capabilities);
        }
    } else {
        BROOKESIA_LOGW("Failed to query Bluetooth capabilities: %1%", capabilities_result.error());
    }

    {
        std::lock_guard lock(impl_->mutex);
        impl_->state.general_state = GeneralState::Ready;
        impl_->state.is_configured = impl_->config.has_value();
        impl_->profile_requested = false;
        impl_->owns_profile = false;
        reset_runtime_state(impl_->state);
    }
    BROOKESIA_LOGI("Bluetooth speaker service is ready; A2DP remains stopped until requested");
    return true;
}

void BtSpeaker::on_stop()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
    if (!impl_ || !callback_context_) {
        return;
    }

    auto context = callback_context_;
    context->active.store(false);
    context->generation.fetch_add(1);
    std::lock_guard gate(context->gate);
    impl_->bt_connections.clear();

    bool should_stop = false;
    {
        std::lock_guard lock(impl_->mutex);
        should_stop = impl_->owns_profile;
        impl_->profile_requested = false;
    }
    if (should_stop) {
        auto stop_result = BtHelper::call_function_sync(BtHelper::FunctionId::A2dpSinkStop);
        if (!stop_result) {
            BROOKESIA_LOGW("Failed to stop A2DP profile during service cleanup: %1%", stop_result.error());
        }
    }

    {
        std::lock_guard lock(impl_->mutex);
        impl_->state = {};
        impl_->state.general_state = GeneralState::Idle;
        impl_->state.is_configured = impl_->config.has_value();
        impl_->owns_profile = false;
    }
}

std::expected<void, std::string> BtSpeaker::function_set_config(const boost::json::object &config_json)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);

    Config config;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(config_json, config) || config.device.device_name.empty()) {
        return std::unexpected("Invalid Bluetooth speaker configuration");
    }

    {
        std::lock_guard lock(impl_->mutex);
        const bool is_busy = impl_->owns_profile || impl_->profile_requested ||
                             impl_->state.general_state == GeneralState::Starting ||
                             impl_->state.general_state == GeneralState::Started ||
                             impl_->state.general_state == GeneralState::Stopping;
        if (is_busy && (!impl_->config || *impl_->config != config)) {
            return std::unexpected("Bluetooth speaker configuration cannot change while started");
        }
        impl_->config = std::move(config);
        impl_->state.is_configured = true;
        if (impl_->state.general_state == GeneralState::Error && !is_busy) {
            impl_->state.general_state = GeneralState::Ready;
        }
    }
    publish_state_changed();
    return {};
}

std::expected<boost::json::object, std::string> BtSpeaker::function_get_config()
{
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);
    std::lock_guard lock(impl_->mutex);
    if (!impl_->config) {
        return std::unexpected("Bluetooth speaker is not configured");
    }
    return BROOKESIA_DESCRIBE_TO_JSON(*impl_->config).as_object();
}

std::expected<void, std::string> BtSpeaker::function_start()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);

    Config config;
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->config) {
            return std::unexpected("Bluetooth speaker must be configured before starting");
        }
        if (impl_->state.is_started && impl_->state.general_state == GeneralState::Started) {
            return {};
        }
        if (impl_->owns_profile) {
            return std::unexpected("Bluetooth speaker profile must be stopped before restarting");
        }
        config = *impl_->config;
        impl_->profile_requested = true;
        impl_->state.general_state = GeneralState::Starting;
    }
    if (!refresh_bt_event_subscriptions()) {
        {
            std::lock_guard lock(impl_->mutex);
            impl_->profile_requested = false;
            impl_->state.general_state = GeneralState::Error;
        }
        publish_state_changed();
        publish_error("Start", -1, "Failed to refresh Bt event subscriptions");
        return std::unexpected("Failed to subscribe to Bt profile events");
    }
    publish_state_changed();

    auto config_result = BtHelper::call_function_sync(
                             BtHelper::FunctionId::SetDeviceConfig,
                             BROOKESIA_DESCRIBE_TO_JSON(config.device).as_object()
                         );
    if (!config_result) {
        {
            std::lock_guard lock(impl_->mutex);
            impl_->profile_requested = false;
            impl_->state.general_state = GeneralState::Error;
        }
        static_cast<void>(refresh_bt_event_subscriptions());
        publish_state_changed();
        publish_error("SetDeviceConfig", -1, config_result.error());
        return std::unexpected("Failed to configure Bluetooth device: " + config_result.error());
    }

    auto start_result = BtHelper::call_function_sync(BtHelper::FunctionId::A2dpSinkStart);
    if (!start_result) {
        static_cast<void>(BtHelper::call_function_sync(BtHelper::FunctionId::A2dpSinkStop));
        {
            std::lock_guard lock(impl_->mutex);
            impl_->profile_requested = false;
            impl_->owns_profile = false;
            impl_->state.is_started = false;
            impl_->state.general_state = GeneralState::Error;
        }
        static_cast<void>(refresh_bt_event_subscriptions());
        publish_state_changed();
        publish_error("Start", -1, start_result.error());
        return std::unexpected(start_result.error());
    }

    {
        std::lock_guard lock(impl_->mutex);
        impl_->owns_profile = true;
        impl_->state.is_supported = true;
        impl_->state.is_started = true;
        impl_->state.general_state = GeneralState::Started;
    }
    auto volume_result = BtHelper::call_function_sync<double>(BtHelper::FunctionId::A2dpGetVolume);
    if (volume_result && is_valid_volume(*volume_result)) {
        std::lock_guard lock(impl_->mutex);
        impl_->state.volume = static_cast<uint8_t>(*volume_result);
    } else if (volume_result) {
        publish_error("GetVolume", -1, "Bt returned an invalid volume");
    }
    publish_state_changed();
    return {};
}

std::expected<void, std::string> BtSpeaker::function_stop()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);

    bool should_stop = false;
    {
        std::lock_guard lock(impl_->mutex);
        should_stop = impl_->owns_profile;
        impl_->profile_requested = false;
        if (should_stop) {
            impl_->state.general_state = GeneralState::Stopping;
        }
    }
    const bool subscriptions_ready = refresh_bt_event_subscriptions();
    if (should_stop) {
        publish_state_changed();
    }

    if (should_stop) {
        auto stop_result = BtHelper::call_function_sync(BtHelper::FunctionId::A2dpSinkStop);
        if (!stop_result) {
            {
                std::lock_guard lock(impl_->mutex);
                impl_->profile_requested = true;
                impl_->state.general_state = GeneralState::Error;
            }
            publish_state_changed();
            publish_error("Stop", -1, stop_result.error());
            return std::unexpected(stop_result.error());
        }
    }

    {
        std::lock_guard lock(impl_->mutex);
        const bool supported = impl_->state.is_supported;
        const bool configured = impl_->state.is_configured;
        impl_->state = {};
        impl_->state.general_state = GeneralState::Ready;
        impl_->state.is_supported = supported;
        impl_->state.is_configured = configured;
        impl_->owns_profile = false;
        impl_->profile_requested = false;
    }
    publish_state_changed();
    if (!subscriptions_ready) {
        publish_error("Stop", -1, "Failed to refresh Bt event subscriptions");
    }
    return {};
}

std::expected<boost::json::object, std::string> BtSpeaker::function_get_state()
{
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);
    std::lock_guard lock(impl_->mutex);
    return BROOKESIA_DESCRIBE_TO_JSON(impl_->state).as_object();
}

std::expected<void, std::string> BtSpeaker::function_pause()
{
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->state.is_started || !impl_->state.is_connected) {
            return std::unexpected("Bluetooth speaker is not connected");
        }
    }
    auto result = BtHelper::call_function_sync(BtHelper::FunctionId::A2dpPause);
    if (!result) {
        publish_error("Pause", -1, result.error());
    }
    return result;
}

std::expected<void, std::string> BtSpeaker::function_resume()
{
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->state.is_started || !impl_->state.is_connected) {
            return std::unexpected("Bluetooth speaker is not connected");
        }
    }
    auto result = BtHelper::call_function_sync(BtHelper::FunctionId::A2dpResume);
    if (!result) {
        publish_error("Resume", -1, result.error());
    }
    return result;
}

std::expected<void, std::string> BtSpeaker::function_next()
{
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->state.is_started || !impl_->state.is_connected) {
            return std::unexpected("Bluetooth speaker is not connected");
        }
    }
    auto result = BtHelper::call_function_sync(BtHelper::FunctionId::A2dpNext);
    if (!result) {
        publish_error("Next", -1, result.error());
    }
    return result;
}

std::expected<void, std::string> BtSpeaker::function_previous()
{
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->state.is_started || !impl_->state.is_connected) {
            return std::unexpected("Bluetooth speaker is not connected");
        }
    }
    auto result = BtHelper::call_function_sync(BtHelper::FunctionId::A2dpPrevious);
    if (!result) {
        publish_error("Previous", -1, result.error());
    }
    return result;
}

std::expected<void, std::string> BtSpeaker::function_set_volume(double volume)
{
    if (!is_valid_volume(volume)) {
        return std::unexpected("Volume must be an integer in the range 0..100");
    }
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->state.is_started) {
            return std::unexpected("Bluetooth speaker is not started");
        }
    }
    auto result = BtHelper::call_function_sync(BtHelper::FunctionId::A2dpSetVolume, volume);
    if (!result) {
        publish_error("SetVolume", -1, result.error());
        return result;
    }
    bool changed = false;
    {
        std::lock_guard lock(impl_->mutex);
        changed = impl_->state.volume != static_cast<uint8_t>(volume);
        impl_->state.volume = static_cast<uint8_t>(volume);
    }
    if (changed) {
        publish_event(
            BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::VolumeChanged),
            std::vector<EventItem> {volume}
        );
        publish_state_changed();
    }
    return result;
}

std::expected<double, std::string> BtSpeaker::function_get_volume()
{
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->state.is_started) {
            return std::unexpected("Bluetooth speaker is not started");
        }
    }
    auto result = BtHelper::call_function_sync<double>(BtHelper::FunctionId::A2dpGetVolume);
    if (!result) {
        publish_error("GetVolume", -1, result.error());
        return std::unexpected(result.error());
    }
    if (!is_valid_volume(*result)) {
        publish_error("GetVolume", -1, "Bt returned an invalid volume");
        return std::unexpected("Bt returned an invalid volume");
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->state.volume = static_cast<uint8_t>(*result);
    }
    return result;
}

std::expected<void, std::string> BtSpeaker::function_disconnect()
{
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return std::unexpected("Bluetooth speaker service is not running");
    }
    std::lock_guard gate(context->gate);
    std::optional<BtHelper::PeerInfo> connection;
    {
        std::lock_guard lock(impl_->mutex);
        connection = impl_->state.connection;
    }
    if (!connection) {
        return std::unexpected("Bluetooth speaker is not connected");
    }
    auto result = BtHelper::call_function_sync(
                      BtHelper::FunctionId::Disconnect, static_cast<double>(connection->connection_id)
                  );
    if (!result) {
        publish_error("Disconnect", -1, result.error());
    }
    return result;
}

bool BtSpeaker::refresh_bt_event_subscriptions()
{
    auto context = callback_context_;
    if (!context || !context->active.load()) {
        return false;
    }

    const auto generation = context->generation.fetch_add(1) + 1;
    impl_->bt_connections.clear();
    if (subscribe_bt_events(generation, context)) {
        return true;
    }

    context->generation.fetch_add(1);
    impl_->bt_connections.clear();
    return false;
}

bool BtSpeaker::subscribe_bt_events(uint64_t generation, const std::shared_ptr<CallbackContext> &context)
{
    std::weak_ptr<CallbackContext> weak_context = context;
    auto add = [this](EventRegistry::SignalConnection connection) {
        if (!connection.connected()) {
            return false;
        }
        impl_->bt_connections.push_back(std::move(connection));
        return true;
    };

    bool result = true;
    result = add(BtHelper::subscribe_event(
                     BtHelper::EventId::StateChanged,
    [this, weak_context, generation](const std::string &, const boost::json::object & state) {
        post_bt_event(weak_context, generation, [this, state]() {
            handle_bt_state_changed(state);
        });
    }
                 )) &&result;
    result = add(BtHelper::subscribe_event(
                     BtHelper::EventId::ProfileAvailabilityChanged,
    [this, weak_context, generation](const std::string &, const std::string & profile, bool available) {
        post_bt_event(weak_context, generation, [this, profile, available]() {
            handle_profile_availability_changed(profile, available);
        });
    }
                 )) &&result;
    result = add(BtHelper::subscribe_event(
                     BtHelper::EventId::ConnectionStateChanged,
                     [this, weak_context, generation](const std::string &, const std::string & state,
    const boost::json::object & connection) {
        post_bt_event(weak_context, generation, [this, state, connection]() {
            handle_connection_state_changed(state, connection);
        });
    }
                 )) &&result;
    result = add(BtHelper::subscribe_event(
                     BtHelper::EventId::A2dpStreamStateChanged,
    [this, weak_context, generation](const std::string &, const std::string & state) {
        post_bt_event(weak_context, generation, [this, state]() {
            handle_stream_state_changed(state);
        });
    }
                 )) &&result;
    result = add(BtHelper::subscribe_event(
                     BtHelper::EventId::PlaybackStatusChanged,
    [this, weak_context, generation](const std::string &, const std::string & status) {
        post_bt_event(weak_context, generation, [this, status]() {
            handle_playback_status_changed(status);
        });
    }
                 )) &&result;
    result = add(BtHelper::subscribe_event(
                     BtHelper::EventId::MetadataChanged,
    [this, weak_context, generation](const std::string &, const boost::json::object & metadata) {
        post_bt_event(weak_context, generation, [this, metadata]() {
            handle_metadata_changed(metadata);
        });
    }
                 )) &&result;
    result = add(BtHelper::subscribe_event(
                     BtHelper::EventId::VolumeChanged,
    [this, weak_context, generation](const std::string &, double volume) {
        post_bt_event(weak_context, generation, [this, volume]() {
            handle_volume_changed(volume);
        });
    }
                 )) &&result;
    result = add(BtHelper::subscribe_event(
                     BtHelper::EventId::ErrorHappened,
                     [this, weak_context, generation](const std::string &, const std::string & operation, double code,
    const std::string & message) {
        post_bt_event(weak_context, generation, [this, operation, code, message]() {
            handle_bt_error(operation, code, message);
        });
    }
                 )) &&result;
    return result;
}

void BtSpeaker::post_bt_event(
    const std::weak_ptr<CallbackContext> &weak_context, uint64_t generation,
    std::function<void()> callback
)
{
    auto context = weak_context.lock();
    if (!context || !context->active.load() || generation != context->generation.load()) {
        return;
    }
    auto scheduler = get_task_scheduler();
    if (!scheduler) {
        BROOKESIA_LOGW("Dropping Bt event because the service task scheduler is unavailable");
        return;
    }
    const bool posted = scheduler->post([this, weak_context, generation, callback = std::move(callback)]() mutable {
        auto context = weak_context.lock();
        if (!context)
        {
            return;
        }
        std::lock_guard gate(context->gate);
        if (!context->active.load() || generation != context->generation.load() || !is_running() || !impl_)
        {
            return;
        }
        callback();
    }, nullptr, get_call_task_group());
    if (!posted) {
        BROOKESIA_LOGW("Failed to post Bt event to the Bluetooth speaker task group");
    }
}

void BtSpeaker::handle_bt_state_changed(const boost::json::object &state_json)
{
    BtHelper::State bt_state;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(state_json, bt_state)) {
        publish_error("BtStateChanged", -1, "Invalid Bt state event");
        return;
    }
    {
        std::lock_guard lock(impl_->mutex);
        const bool a2dp_available =
            std::find(bt_state.profiles.begin(), bt_state.profiles.end(), BtHelper::Profile::A2dpSink) !=
            bt_state.profiles.end();
        impl_->state.is_supported = bt_state.is_supported && a2dp_available;
        if (impl_->profile_requested && bt_state.host_state == BtHelper::HostState::Error) {
            impl_->state.general_state = GeneralState::Error;
        }
    }
    publish_state_changed();
}

void BtSpeaker::handle_profile_availability_changed(const std::string &profile, bool available)
{
    BtHelper::Profile parsed_profile;
    if (!BROOKESIA_DESCRIBE_STR_TO_ENUM(profile, parsed_profile) || parsed_profile != BtHelper::Profile::A2dpSink) {
        return;
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->state.is_supported = available;
        if (!available && impl_->profile_requested) {
            impl_->state.general_state = GeneralState::Error;
        }
    }
    publish_state_changed();
}

void BtSpeaker::handle_connection_state_changed(
    const std::string &state, const boost::json::object &connection_json
)
{
    BtHelper::ConnectionState connection_state;
    BtHelper::PeerInfo connection;
    if (!BROOKESIA_DESCRIBE_STR_TO_ENUM(state, connection_state) ||
            !BROOKESIA_DESCRIBE_FROM_JSON(connection_json, connection)) {
        publish_error("ConnectionStateChanged", -1, "Invalid Bt connection event");
        return;
    }

    bool should_stop_local_playback = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->profile_requested || !impl_->owns_profile) {
            return;
        }
        const bool was_connected = impl_->state.is_connected;
        impl_->state.connection_state = connection_state;
        impl_->state.is_connected = connection_state == BtHelper::ConnectionState::Connected;
        if (connection_state == BtHelper::ConnectionState::Disconnected) {
            impl_->state.connection.reset();
            impl_->state.stream_state = BtHelper::StreamState::Idle;
            impl_->state.playback_status = BtHelper::PlaybackStatus::Unknown;
            impl_->state.is_music_active = false;
            impl_->state.metadata = {};
        } else {
            impl_->state.connection = connection;
        }
        should_stop_local_playback = !was_connected && impl_->state.is_connected && impl_->state.is_started &&
                                     impl_->config &&
                                     impl_->config->stop_local_playback_on_connect;
    }

    publish_event(
        BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::ConnectionStateChanged),
    {BROOKESIA_DESCRIBE_TO_STR(connection_state), connection_json}
    );
    publish_state_changed();
    if (should_stop_local_playback) {
        stop_local_playback_async();
    }
}

void BtSpeaker::handle_stream_state_changed(const std::string &state)
{
    BtHelper::StreamState stream_state;
    if (!BROOKESIA_DESCRIBE_STR_TO_ENUM(state, stream_state)) {
        publish_error("StreamStateChanged", -1, "Invalid Bt stream event");
        return;
    }
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->profile_requested || !impl_->owns_profile) {
            return;
        }
        impl_->state.stream_state = stream_state;
        impl_->state.is_music_active = is_music_active(stream_state, impl_->state.playback_status);
    }
    publish_event(BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::StreamStateChanged), {state});
    publish_state_changed();
}

void BtSpeaker::handle_playback_status_changed(const std::string &status)
{
    BtHelper::PlaybackStatus playback_status;
    if (!BROOKESIA_DESCRIBE_STR_TO_ENUM(status, playback_status)) {
        publish_error("PlaybackStatusChanged", -1, "Invalid Bt playback event");
        return;
    }
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->profile_requested || !impl_->owns_profile) {
            return;
        }
        impl_->state.playback_status = playback_status;
        impl_->state.is_music_active = is_music_active(impl_->state.stream_state, playback_status);
    }
    publish_event(BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::PlaybackStatusChanged), {status});
    publish_state_changed();
}

void BtSpeaker::handle_metadata_changed(const boost::json::object &metadata_json)
{
    BtHelper::TrackMetadata metadata;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(metadata_json, metadata)) {
        publish_error("MetadataChanged", -1, "Invalid Bt metadata event");
        return;
    }
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->profile_requested || !impl_->owns_profile) {
            return;
        }
        impl_->state.metadata = metadata;
    }
    publish_event(
        BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::MetadataChanged),
        std::vector<EventItem> {metadata_json}
    );
    publish_state_changed();
}

void BtSpeaker::handle_volume_changed(double volume)
{
    if (!is_valid_volume(volume)) {
        publish_error("VolumeChanged", -1, "Invalid Bt volume event");
        return;
    }
    bool changed = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->profile_requested || !impl_->owns_profile) {
            return;
        }
        changed = impl_->state.volume != static_cast<uint8_t>(volume);
        impl_->state.volume = static_cast<uint8_t>(volume);
    }
    if (!changed) {
        return;
    }
    publish_event(
        BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::VolumeChanged),
        std::vector<EventItem> {volume}
    );
    publish_state_changed();
}

void BtSpeaker::handle_bt_error(const std::string &operation, double code, const std::string &message)
{
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->profile_requested && !impl_->owns_profile) {
            return;
        }
    }
    publish_error(operation, static_cast<int>(code), message);
}

void BtSpeaker::stop_local_playback_async()
{
    using AudioPlaybackHelper = helper::AudioPlayback;
    if (!AudioPlaybackHelper::is_running()) {
        return;
    }
    if (!AudioPlaybackHelper::call_function_async(AudioPlaybackHelper::FunctionId::Stop)) {
        publish_error("StopLocalPlayback", -1, "Failed to submit local AudioPlayback stop request");
    }
}

void BtSpeaker::publish_state_changed()
{
    auto state_result = function_get_state();
    if (state_result) {
        publish_event(
            BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::StateChanged),
            std::vector<EventItem> {state_result.value()}
        );
    }
}

void BtSpeaker::publish_error(const std::string &operation, int code, const std::string &message)
{
    publish_event(
        BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::ErrorHappened),
    {operation, static_cast<double>(code), message}
    );
}

#if BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_AUTO_REGISTER
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    ServiceBase, BtSpeaker, BtSpeaker::get_instance().get_attributes().name, BtSpeaker::get_instance(),
    BROOKESIA_SERVICE_BT_SPEAKER_PLUGIN_SYMBOL
);
#endif

} // namespace esp_brookesia::service
