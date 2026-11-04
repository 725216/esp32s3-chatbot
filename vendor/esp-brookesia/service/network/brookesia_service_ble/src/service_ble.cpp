/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include "brookesia/service_ble/macro_configs.h"
#if !BROOKESIA_SERVICE_BLE_SERVICE_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "brookesia/lib_utils/plugin.hpp"
#include "brookesia/service_ble/service_ble.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::service::ble {

std::string Ble::get_component_version()
{
    return make_version(
               BROOKESIA_SERVICE_BLE_VER_MAJOR, BROOKESIA_SERVICE_BLE_VER_MINOR,
               BROOKESIA_SERVICE_BLE_VER_PATCH
           );
}

bool Ble::on_init()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_LOGI(
        "Version: %1%.%2%.%3%", BROOKESIA_SERVICE_BLE_VER_MAJOR, BROOKESIA_SERVICE_BLE_VER_MINOR,
        BROOKESIA_SERVICE_BLE_VER_PATCH
    );

    auto peripheral_iface = hal::acquire_first_interface<hal::bluetooth::ble::PeripheralIface>();
    BROOKESIA_CHECK_NULL_RETURN(peripheral_iface, false, "BLE Peripheral HAL interface is not available");

    BROOKESIA_LOGI("Using BLE Peripheral interface: %1%", peripheral_iface.instance_name());
    peripheral_iface_ = std::move(peripheral_iface);
    set_general_state(GeneralState::Idle, false);
    return true;
}

void Ble::on_deinit()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    stop_host(false);
    peripheral_iface_.reset();
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = {};
    is_configured_ = false;
    general_state_ = GeneralState::Idle;
}

bool Ble::on_start()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    set_general_state(GeneralState::Ready, false);
    BROOKESIA_LOGI("BLE service is ready; advertising remains stopped until requested");
    return true;
}

void Ble::on_stop()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    /* ServiceBase::stop() holds its state mutex while this hook runs. Clearing
     * callbacks before stopping the backend prevents a synchronous HAL callback
     * from publishing an event and trying to re-enter that mutex. */
    stop_host(false);
    set_general_state(GeneralState::Idle, false);
}

std::expected<void, std::string> Ble::function_set_peripheral_config(const boost::json::object &config_json)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    PeripheralConfig config;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(config_json, config)) {
        return std::unexpected("Invalid BLE peripheral configuration JSON");
    }

    std::string validation_error;
    if (!hal::bluetooth::ble::validate_peripheral_config(config, &validation_error)) {
        return std::unexpected("Invalid BLE peripheral configuration: " + validation_error);
    }
    config = hal::bluetooth::ble::normalize_peripheral_config(config);

    if (is_advertising_.load() || !get_connections().empty()) {
        return std::unexpected("BLE peripheral configuration is busy while advertising or connected");
    }

    bool host_active = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        host_active = hal_configured_ || hal_initialized_ || hal_started_;
    }
    if (host_active && !stop_host(true)) {
        return std::unexpected("Failed to stop the BLE host before reconfiguration");
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = std::move(config);
        is_configured_ = true;
    }
    set_general_state(GeneralState::Ready);
    BROOKESIA_LOGI("BLE peripheral configuration updated");
    return {};
}

std::expected<boost::json::object, std::string> Ble::function_get_peripheral_config()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_configured_) {
        return std::unexpected("BLE peripheral configuration is not set");
    }
    return BROOKESIA_DESCRIBE_TO_JSON(config_).as_object();
}

std::expected<void, std::string> Ble::function_trigger_advertising_start()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_configured_) {
            return std::unexpected("BLE peripheral configuration is not set");
        }
    }
    if (is_advertising_.load() || advertising_start_pending_.exchange(true)) {
        return {};
    }
    if (!get_connections().empty()) {
        advertising_start_pending_.store(false);
        return std::unexpected("BLE advertising cannot start while a client is connected");
    }
    if (!start_host()) {
        advertising_start_pending_.store(false);
        return std::unexpected("Failed to start the BLE host");
    }
    if (!peripheral_iface_->start_advertising()) {
        advertising_start_pending_.store(false);
        publish_error("StartAdvertising", -1, "BLE HAL rejected the advertising request");
        (void)stop_host(false);
        set_general_state(GeneralState::Error);
        return std::unexpected("Failed to start BLE advertising");
    }

    /* Some backends accept the request before the radio/host is ready. Only the
     * HAL callback represents the actual advertising state; keeping the cached
     * state false here also allows a retry when a deferred start later fails. */
    return {};
}

std::expected<void, std::string> Ble::function_trigger_advertising_stop()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    bool host_started = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        host_started = hal_started_;
    }
    if (!host_started) {
        return {};
    }
    advertising_start_pending_.store(false);
    if (!peripheral_iface_->stop_advertising()) {
        return std::unexpected("Failed to stop BLE advertising");
    }
    is_advertising_.store(false);
    return {};
}

std::expected<boost::json::object, std::string> Ble::function_get_state()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    State state;
    state.connections = get_connections();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state.general_state = general_state_;
        state.is_configured = is_configured_;
    }
    state.is_advertising = is_advertising_.load();
    return BROOKESIA_DESCRIBE_TO_JSON(state).as_object();
}

std::expected<boost::json::array, std::string> Ble::function_get_connections()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    return BROOKESIA_DESCRIBE_TO_JSON(get_connections()).as_array();
}

std::expected<void, std::string> Ble::function_notify(
    double connection_id_value, const std::string &service_uuid, const std::string &characteristic_uuid,
    const boost::json::array &data_json
)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    auto connection_id = parse_connection_id(connection_id_value);
    if (!connection_id) {
        return std::unexpected(connection_id.error());
    }
    auto data = parse_byte_array(data_json);
    if (!data) {
        return std::unexpected(data.error());
    }
    if (!hal::bluetooth::ble::is_valid_uuid(service_uuid) || !hal::bluetooth::ble::is_valid_uuid(characteristic_uuid)) {
        return std::unexpected("ServiceUuid and CharacteristicUuid must be canonical 128-bit UUIDs");
    }

    CharacteristicId characteristic{
        .service_uuid = hal::bluetooth::ble::normalize_uuid(service_uuid),
        .characteristic_uuid = hal::bluetooth::ble::normalize_uuid(characteristic_uuid),
    };

    const auto requested_connection_id = *connection_id;
    const auto connections = get_connections();
    const auto connection = std::find_if(
    connections.begin(), connections.end(), [requested_connection_id](const ConnectionInfo & item) {
        return item.connection_id == requested_connection_id;
    }
                            );
    if (connection == connections.end()) {
        return std::unexpected("BLE connection does not exist");
    }

    bool is_notifiable = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &service : config_.services) {
            if (service.uuid != characteristic.service_uuid) {
                continue;
            }
            for (const auto &candidate : service.characteristics) {
                if (candidate.uuid == characteristic.characteristic_uuid) {
                    is_notifiable = candidate.notify;
                    break;
                }
            }
        }
    }
    if (!is_notifiable) {
        return std::unexpected("Target BLE characteristic is not configured for notifications");
    }
    if (!peripheral_iface_->is_subscribed(*connection_id, characteristic)) {
        return std::unexpected("BLE client is not subscribed to the target characteristic");
    }
    const auto max_payload = static_cast<std::size_t>(connection->mtu - 3);
    if (data->size() > max_payload) {
        return std::unexpected(
                   "Notification data exceeds the negotiated ATT payload limit of " +
                   std::to_string(max_payload) + " bytes"
               );
    }
    if (!peripheral_iface_->notify(*connection_id, characteristic, *data)) {
        return std::unexpected("Failed to send BLE GATT notification");
    }
    return {};
}

std::expected<void, std::string> Ble::function_disconnect(double connection_id_value)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    auto connection_id = parse_connection_id(connection_id_value);
    if (!connection_id) {
        return std::unexpected(connection_id.error());
    }
    const auto requested_connection_id = *connection_id;
    const auto connections = get_connections();
    const bool connection_exists = std::ranges::any_of(
    connections, [requested_connection_id](const ConnectionInfo & connection) {
        return connection.connection_id == requested_connection_id;
    }
                                   );
    if (!connection_exists) {
        return std::unexpected("BLE connection does not exist");
    }
    if (!peripheral_iface_->disconnect(*connection_id)) {
        return std::unexpected("Failed to disconnect BLE client");
    }
    return {};
}

bool Ble::start_host()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    PeripheralConfig config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (hal_started_) {
            return true;
        }
        if (!is_configured_) {
            return false;
        }
        config = config_;
    }

    set_general_state(GeneralState::Starting);
    const auto callback_generation = callback_generation_.fetch_add(1) + 1;
    if (!peripheral_iface_->configure(config, make_hal_callbacks(callback_generation))) {
        callback_generation_.fetch_add(1);
        (void)peripheral_iface_->clear_callbacks();
        publish_error("Configure", -1, "Failed to configure BLE HAL");
        set_general_state(GeneralState::Error);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hal_configured_ = true;
    }

    if (!peripheral_iface_->init()) {
        publish_error("Init", -1, "Failed to initialize BLE HAL");
        stop_host(false);
        set_general_state(GeneralState::Error);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hal_initialized_ = true;
    }

    if (!peripheral_iface_->start()) {
        publish_error("Start", -1, "Failed to start BLE HAL");
        stop_host(false);
        set_general_state(GeneralState::Error);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hal_started_ = true;
    }
    set_general_state(GeneralState::Started);
    BROOKESIA_LOGI("BLE host started");
    return true;
}

bool Ble::stop_host(bool publish_state)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (!peripheral_iface_) {
        return true;
    }

    callback_generation_.fetch_add(1);

    bool configured = false;
    bool initialized = false;
    bool started = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        configured = hal_configured_;
        initialized = hal_initialized_;
        started = hal_started_;
    }
    if (!configured && !initialized && !started) {
        (void)peripheral_iface_->clear_callbacks();
        if (!publish_state) {
            set_general_state(GeneralState::Idle, false);
        }
        return true;
    }

    if (publish_state) {
        set_general_state(GeneralState::Stopping);
    }

    bool result = true;
    if (started || is_advertising_.load() || advertising_start_pending_.load()) {
        result = peripheral_iface_->stop_advertising() && result;
    }
    for (const auto &connection : peripheral_iface_->get_connections()) {
        result = peripheral_iface_->disconnect(connection.connection_id) && result;
    }
    if (started) {
        result = peripheral_iface_->stop() && result;
    }
    if (initialized) {
        result = peripheral_iface_->deinit() && result;
    }
    result = peripheral_iface_->clear_callbacks() && result;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        hal_configured_ = false;
        hal_initialized_ = false;
        hal_started_ = false;
    }
    is_advertising_.store(false);
    advertising_start_pending_.store(false);
    set_general_state(publish_state ? GeneralState::Ready : GeneralState::Idle, publish_state);
    BROOKESIA_LOGI("BLE host stopped");
    return result;
}

hal::bluetooth::ble::PeripheralIface::Callbacks Ble::make_hal_callbacks(uint64_t generation)
{
    return {
        .on_advertising_state_changed = [this, generation](bool is_advertising)
        {
            post_hal_callback(generation, [this, is_advertising]() {
                advertising_start_pending_.store(false);
                is_advertising_.store(is_advertising);
                publish_advertising_state(is_advertising);
            });
        },
        .on_connection_state_changed = [this, generation](
                                           const ConnectionInfo & connection, bool is_connected, const std::string &reason
                                       )
        {
            post_hal_callback(generation, [this, connection, is_connected, reason]() {
                publish_connection_state(connection, is_connected, reason);
            });
        },
        .on_mtu_changed = [this, generation](uint16_t connection_id, uint16_t mtu)
        {
            post_hal_callback(generation, [this, connection_id, mtu]() {
                publish_mtu_changed(connection_id, mtu);
            });
        },
        .on_subscription_changed = [this, generation](
                                       uint16_t connection_id, const CharacteristicId & characteristic, bool notify_enabled
                                   )
        {
            post_hal_callback(generation, [this, connection_id, characteristic, notify_enabled]() {
                publish_subscription_changed(connection_id, characteristic, notify_enabled);
            });
        },
        .on_characteristic_written = [this, generation](const WriteEvent & event)
        {
            post_hal_callback(generation, [this, event]() {
                publish_characteristic_written(event);
            });
        },
        .on_error = [this, generation](const std::string & operation, int code, const std::string & message)
        {
            post_hal_callback(generation, [this, operation, code, message]() {
                publish_error(operation, code, message);
                if ((operation == "start_advertising") || (operation == "sync")) {
                    advertising_start_pending_.store(false);
                    is_advertising_.store(false);
                    (void)stop_host(false);
                    set_general_state(GeneralState::Error);
                }
            });
        },
    };
}

void Ble::post_hal_callback(uint64_t generation, std::function<void()> callback)
{
    if (generation != callback_generation_.load()) {
        return;
    }

    auto scheduler = get_task_scheduler();
    if (!scheduler) {
        BROOKESIA_LOGW("Dropping BLE HAL callback because the task scheduler is unavailable");
        return;
    }
    const bool posted = scheduler->post([this, generation, callback = std::move(callback)]() mutable {
        if ((generation == callback_generation_.load()) && is_running())
        {
            callback();
        }
    }, nullptr, get_call_task_group());
    if (!posted) {
        BROOKESIA_LOGW("Failed to post BLE HAL callback to the service task group");
    }
}

std::vector<Ble::ConnectionInfo> Ble::get_connections() const
{
    if (!peripheral_iface_) {
        return {};
    }
    bool initialized = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        initialized = hal_initialized_;
    }
    return initialized ? peripheral_iface_->get_connections() : std::vector<ConnectionInfo> {};
}

void Ble::set_general_state(GeneralState state, bool should_publish)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        changed = (general_state_ != state);
        general_state_ = state;
    }
    if (changed && should_publish) {
        if (!publish_event(BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::GeneralStateChanged), std::vector<EventItem> {
        BROOKESIA_DESCRIBE_TO_STR(state)
        })) {
            BROOKESIA_LOGE("Failed to publish BLE general state event");
        }
    }
}

void Ble::publish_advertising_state(bool is_advertising)
{
    if (!publish_event(BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::AdvertisingStateChanged), std::vector<EventItem> {
    is_advertising
})) {
        BROOKESIA_LOGE("Failed to publish BLE advertising state event");
    }
}

void Ble::publish_connection_state(
    const ConnectionInfo &connection, bool is_connected, const std::string &reason
)
{
    if (!publish_event(BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::ConnectionStateChanged), std::vector<EventItem> {
    BROOKESIA_DESCRIBE_TO_JSON(connection).as_object(), is_connected, reason
    })) {
        BROOKESIA_LOGE("Failed to publish BLE connection state event");
    }
}

void Ble::publish_mtu_changed(uint16_t connection_id, uint16_t mtu)
{
    if (!publish_event(BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::MtuChanged), std::vector<EventItem> {
    static_cast<double>(connection_id), static_cast<double>(mtu)
    })) {
        BROOKESIA_LOGE("Failed to publish BLE MTU event");
    }
}

void Ble::publish_subscription_changed(
    uint16_t connection_id, const CharacteristicId &characteristic, bool notify_enabled
)
{
    if (!publish_event(BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::SubscriptionChanged), std::vector<EventItem> {
    static_cast<double>(connection_id), characteristic.service_uuid, characteristic.characteristic_uuid,
        notify_enabled
    })) {
        BROOKESIA_LOGE("Failed to publish BLE subscription event");
    }
}

void Ble::publish_characteristic_written(const WriteEvent &event)
{
    boost::json::array data;
    data.reserve(event.data.size());
    for (uint8_t byte : event.data) {
        data.emplace_back(byte);
    }
    if (!publish_event(BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::CharacteristicWritten), std::vector<EventItem> {
    static_cast<double>(event.connection_id), event.characteristic.service_uuid,
        event.characteristic.characteristic_uuid, std::move(data)
    })) {
        BROOKESIA_LOGE("Failed to publish BLE characteristic write event");
    }
}

void Ble::publish_error(const std::string &operation, int code, const std::string &message)
{
    if (!publish_event(BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::ErrorHappened), std::vector<EventItem> {
    operation, static_cast<double>(code), message
    })) {
        BROOKESIA_LOGE("Failed to publish BLE error event");
    }
}

std::expected<uint16_t, std::string> Ble::parse_connection_id(double value)
{
    if (!std::isfinite(value) || value < 0 || std::floor(value) != value ||
            value > std::numeric_limits<uint16_t>::max()) {
        return std::unexpected("ConnectionId must be a uint16 integer");
    }
    return static_cast<uint16_t>(value);
}

std::expected<Ble::ByteArray, std::string> Ble::parse_byte_array(const boost::json::array &data)
{
    ByteArray bytes;
    bytes.reserve(data.size());
    for (const auto &item : data) {
        uint64_t value = 0;
        if (item.is_int64()) {
            auto signed_value = item.as_int64();
            if (signed_value < 0) {
                return std::unexpected("Data elements must be integers in the range 0..255");
            }
            value = static_cast<uint64_t>(signed_value);
        } else if (item.is_uint64()) {
            value = item.as_uint64();
        } else {
            return std::unexpected("Data elements must be integers in the range 0..255");
        }
        if (value > std::numeric_limits<uint8_t>::max()) {
            return std::unexpected("Data elements must be integers in the range 0..255");
        }
        bytes.push_back(static_cast<uint8_t>(value));
    }
    return bytes;
}

#if BROOKESIA_SERVICE_BLE_ENABLE_AUTO_REGISTER
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    ServiceBase, Ble, Ble::get_instance().get_attributes().name, Ble::get_instance(),
    BROOKESIA_SERVICE_BLE_PLUGIN_SYMBOL
);
#endif

} // namespace esp_brookesia::service::ble
