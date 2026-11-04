/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <atomic>
#include <expected>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "brookesia/hal_interface/interface.hpp"
#include "brookesia/hal_interface/interfaces/bluetooth/ble/peripheral.hpp"
#include "brookesia/service_ble/macro_configs.h"
#include "brookesia/service_helper/network/ble.hpp"
#include "brookesia/service_manager/service/base.hpp"

namespace esp_brookesia::service::ble {

/**
 * @brief Single-connection BLE Peripheral/GATT Server service.
 */
class Ble: public ServiceBase {
public:
    static Ble &get_instance()
    {
        static Ble instance;
        return instance;
    }

private:
    static std::string get_component_version();

    using Helper = helper::Ble;
    using GeneralState = Helper::GeneralState;
    using State = Helper::State;
    using PeripheralConfig = hal::bluetooth::ble::PeripheralConfig;
    using ConnectionInfo = hal::bluetooth::ble::ConnectionInfo;
    using CharacteristicId = hal::bluetooth::ble::CharacteristicId;
    using ByteArray = hal::bluetooth::ble::ByteArray;
    using WriteEvent = hal::bluetooth::ble::WriteEvent;

    Ble()
        : ServiceBase({
        .name = Helper::get_name().data(),
        .description = "Manage BLE peripheral advertising, GATT connections, writes, and notifications.",
        .version = get_component_version(),
    })
    {}
    ~Ble() = default;

    bool on_init() override;
    void on_deinit() override;
    bool on_start() override;
    void on_stop() override;

    std::expected<void, std::string> function_set_peripheral_config(const boost::json::object &config);
    std::expected<boost::json::object, std::string> function_get_peripheral_config();
    std::expected<void, std::string> function_trigger_advertising_start();
    std::expected<void, std::string> function_trigger_advertising_stop();
    std::expected<boost::json::object, std::string> function_get_state();
    std::expected<boost::json::array, std::string> function_get_connections();
    std::expected<void, std::string> function_notify(
        double connection_id, const std::string &service_uuid, const std::string &characteristic_uuid,
        const boost::json::array &data
    );
    std::expected<void, std::string> function_disconnect(double connection_id);

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
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_1(
                Helper, Helper::FunctionId::SetPeripheralConfig, boost::json::object,
                function_set_peripheral_config(PARAM)
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(
                Helper, Helper::FunctionId::GetPeripheralConfig,
                function_get_peripheral_config()
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(
                Helper, Helper::FunctionId::TriggerAdvertisingStart,
                function_trigger_advertising_start()
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(
                Helper, Helper::FunctionId::TriggerAdvertisingStop,
                function_trigger_advertising_stop()
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(
                Helper, Helper::FunctionId::GetState,
                function_get_state()
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_0(
                Helper, Helper::FunctionId::GetConnections,
                function_get_connections()
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_4(
                Helper, Helper::FunctionId::Notify, double, std::string, std::string, boost::json::array,
                function_notify(PARAM1, PARAM2, PARAM3, PARAM4)
            ),
            BROOKESIA_SERVICE_HELPER_FUNC_HANDLER_1(
                Helper, Helper::FunctionId::Disconnect, double,
                function_disconnect(PARAM)
            ),
        };
    }

    bool start_host();
    bool stop_host(bool publish_state);
    hal::bluetooth::ble::PeripheralIface::Callbacks make_hal_callbacks(uint64_t generation);
    void post_hal_callback(uint64_t generation, std::function<void()> callback);
    std::vector<ConnectionInfo> get_connections() const;
    void set_general_state(GeneralState state, bool should_publish = true);
    void publish_advertising_state(bool is_advertising);
    void publish_connection_state(const ConnectionInfo &connection, bool is_connected, const std::string &reason);
    void publish_mtu_changed(uint16_t connection_id, uint16_t mtu);
    void publish_subscription_changed(
        uint16_t connection_id, const CharacteristicId &characteristic, bool notify_enabled
    );
    void publish_characteristic_written(const WriteEvent &event);
    void publish_error(const std::string &operation, int code, const std::string &message);

    static std::expected<uint16_t, std::string> parse_connection_id(double value);
    static std::expected<ByteArray, std::string> parse_byte_array(const boost::json::array &data);

    hal::InterfaceHandle<hal::bluetooth::ble::PeripheralIface> peripheral_iface_;
    mutable std::mutex mutex_;
    PeripheralConfig config_;
    GeneralState general_state_ = GeneralState::Idle;
    bool is_configured_ = false;
    bool hal_configured_ = false;
    bool hal_initialized_ = false;
    bool hal_started_ = false;
    std::atomic<bool> is_advertising_{false};
    std::atomic<bool> advertising_start_pending_{false};
    std::atomic<uint64_t> callback_generation_{0};
};

} // namespace esp_brookesia::service::ble
