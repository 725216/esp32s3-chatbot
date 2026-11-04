/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#if CONFIG_BT_CONTROLLER_DISABLED
#   include "esp_hosted.h"
#   include "esp_hosted_misc.h"
#endif
#include "brookesia/hal_adaptor/macro_configs.h"
#if !BROOKESIA_HAL_ADAPTOR_BLE_BACKEND_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/utils.hpp"
#include "../host_coordinator.hpp"
#include "backend.hpp"

#if !CONFIG_BT_NIMBLE_ENABLED
#   error "The BLE HAL adaptor requires CONFIG_BT_NIMBLE_ENABLED=y"
#endif

#if !CONFIG_BT_NIMBLE_ROLE_PERIPHERAL || !CONFIG_BT_NIMBLE_GATT_SERVER
#   error "The BLE HAL adaptor requires the NimBLE Peripheral role and GATT Server"
#endif

#if CONFIG_BT_CONTROLLER_DISABLED
#   if CONFIG_BT_NIMBLE_TRANSPORT_UART || !CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE || \
       !CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI
#       error "A controller-disabled BLE HAL adaptor requires ESP-Hosted NimBLE VHCI"
#   endif
#elif !CONFIG_BT_CONTROLLER_ENABLED || !CONFIG_SOC_BLE_SUPPORTED
#   error "The BLE HAL adaptor requires an integrated BLE controller or ESP-Hosted NimBLE VHCI"
#endif

#if !MYNEWT_VAL(BLE_GATT_NOTIFY)
#   error "The BLE HAL adaptor requires NimBLE GATT notifications"
#endif

namespace esp_brookesia::hal {

namespace {

constexpr size_t UUID_BYTE_COUNT = 16;
constexpr int DISCONNECT_WAIT_ITERATIONS = 50;
constexpr int DISCONNECT_WAIT_INTERVAL_MS = 10;

uint8_t hex_to_nibble(char value)
{
    if ((value >= '0') && (value <= '9')) {
        return static_cast<uint8_t>(value - '0');
    }
    if ((value >= 'a') && (value <= 'f')) {
        return static_cast<uint8_t>(value - 'a' + 10);
    }
    return static_cast<uint8_t>(value - 'A' + 10);
}

ble_uuid128_t parse_uuid(const std::string &uuid)
{
    std::array<uint8_t, UUID_BYTE_COUNT> network_order = {};
    size_t nibble_index = 0;
    for (const auto value : uuid) {
        if (value == '-') {
            continue;
        }
        const auto byte_index = nibble_index / 2;
        if ((nibble_index % 2) == 0) {
            network_order[byte_index] = static_cast<uint8_t>(hex_to_nibble(value) << 4);
        } else {
            network_order[byte_index] |= hex_to_nibble(value);
        }
        ++nibble_index;
    }

    ble_uuid128_t result = {};
    result.u.type = BLE_UUID_TYPE_128;
    for (size_t index = 0; index < network_order.size(); ++index) {
        result.value[index] = network_order[network_order.size() - 1 - index];
    }
    return result;
}

std::string format_peer_address(const ble_addr_t &address)
{
    std::array<char, 18> buffer = {};
    snprintf(
        buffer.data(), buffer.size(), "%02x:%02x:%02x:%02x:%02x:%02x",
        address.val[5], address.val[4], address.val[3], address.val[2], address.val[1], address.val[0]
    );
    return buffer.data();
}

} // namespace

class BleEspBackend::Impl {
public:
    struct CharacteristicRuntime {
        Impl *owner = nullptr;
        bluetooth::ble::CharacteristicId id;
        bluetooth::ble::CharacteristicConfig config;
        ble_uuid128_t uuid = {};
        uint16_t value_handle = 0;
    };

    struct ServiceRuntime {
        ble_uuid128_t uuid = {};
        std::vector<std::unique_ptr<CharacteristicRuntime>> characteristics;
        std::vector<ble_gatt_chr_def> definitions;
    };

    ~Impl()
    {
        deinit();
    }

    bool configure(const bluetooth::ble::PeripheralConfig &config, bluetooth::ble::PeripheralIface::Callbacks callbacks)
    {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
        BROOKESIA_LOGD("Params: config(%1%)", BROOKESIA_DESCRIBE_TO_STR(config));

        std::string error_message;
        if (!bluetooth::ble::validate_peripheral_config(config, &error_message)) {
            BROOKESIA_LOGE("Invalid BLE peripheral configuration: %1%", error_message);
            if (callbacks.on_error) {
                callbacks.on_error("configure", BLE_HS_EINVAL, error_message);
            }
            return false;
        }

        std::lock_guard lock(mutex_);
        BROOKESIA_CHECK_FALSE_RETURN(!initialized_ && !started_, false, "Cannot configure an initialized BLE backend");

        config_ = bluetooth::ble::normalize_peripheral_config(config);
        callbacks_ = std::move(callbacks);
        configured_ = true;
        BROOKESIA_LOGI("BLE peripheral configured as '%1%'", config_.device_name);
        return true;
    }

    bool clear_callbacks()
    {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

        std::lock_guard lock(mutex_);
        callbacks_ = {};
        return true;
    }

    bool init()
    {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

        {
            std::lock_guard lock(mutex_);
            BROOKESIA_CHECK_FALSE_RETURN(configured_, false, "BLE backend is not configured");
            if (initialized_) {
                BROOKESIA_LOGD("BLE backend is already initialized, skip");
                return true;
            }
            BROOKESIA_CHECK_FALSE_RETURN(
                (active_instance_ == nullptr) || (active_instance_ == this), false,
                "Another NimBLE peripheral backend is already active"
            );
            active_instance_ = this;
        }
        host_token_ = bluetooth::detail::BluetoothHostCoordinator::get_instance().acquire(
                          bluetooth::detail::BluetoothHostCoordinator::Profile::Ble
                      );

        if (!build_gatt_database()) {
            std::lock_guard lock(mutex_);
            active_instance_ = nullptr;
            host_token_.reset();
            return false;
        }

        if (!prepare_controller()) {
            clear_runtime_after_deinit();
            return false;
        }

        const auto init_result = nimble_port_init();
        if (init_result != ESP_OK) {
            emit_error("init", init_result, "nimble_port_init failed");
            release_controller();
            clear_runtime_after_deinit();
            return false;
        }

        ble_hs_cfg.reset_cb = on_stack_reset;
        ble_hs_cfg.sync_cb = on_stack_sync;
        ble_hs_cfg.gatts_register_cb = on_gatt_registered;
        ble_hs_cfg.store_status_cb = nullptr;
        ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
        ble_hs_cfg.sm_sc = 0;
        ble_hs_cfg.sm_bonding = 0;
        ble_hs_cfg.sm_mitm = 0;

        const auto mtu_result = ble_att_set_preferred_mtu(config_.preferred_mtu);
        if (mtu_result != 0) {
            emit_error("init", mtu_result, "ble_att_set_preferred_mtu failed");
            nimble_port_deinit();
            release_controller();
            clear_runtime_after_deinit();
            return false;
        }

        if (!register_gatt_services()) {
            nimble_port_deinit();
            release_controller();
            clear_runtime_after_deinit();
            return false;
        }

        {
            std::lock_guard lock(mutex_);
            initialized_ = true;
            gatt_needs_readd_ = false;
#if MYNEWT_VAL(BLE_HS_AUTO_START)
            host_auto_start_pending_ = true;
#endif
        }
        BROOKESIA_LOGI("NimBLE peripheral initialized");
        return true;
    }

    bool deinit()
    {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

        bool was_initialized = false;
        {
            std::lock_guard lock(mutex_);
            was_initialized = initialized_;
        }
        if (!was_initialized) {
            BROOKESIA_LOGD("BLE backend is not initialized, skip deinit");
            return true;
        }

        bool success = stop();
        const auto result = nimble_port_deinit();
        if (result != ESP_OK) {
            emit_error("deinit", result, "nimble_port_deinit failed");
            success = false;
        }
        if (!release_controller()) {
            success = false;
        }

        clear_runtime_after_deinit();
        BROOKESIA_LOGI("NimBLE peripheral deinitialized");
        return success;
    }

    bool start()
    {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

        bool readd_services = false;
        {
            std::lock_guard lock(mutex_);
            BROOKESIA_CHECK_FALSE_RETURN(initialized_, false, "BLE backend is not initialized");
            if (started_) {
                BROOKESIA_LOGD("BLE backend is already started, skip");
                return true;
            }
            readd_services = gatt_needs_readd_;
        }

        if (readd_services && !register_gatt_services()) {
            return false;
        }

        {
            std::lock_guard lock(mutex_);
            started_ = true;
            synced_ = false;
            shutting_down_ = false;
            gatt_needs_readd_ = false;
        }

        // The host task self-deletes after nimble_port_stop(). This avoids a quick restart deleting the new task.
        nimble_port_freertos_init(host_task);
#if MYNEWT_VAL(BLE_HS_AUTO_START)
        bool schedule_start = false;
        {
            std::lock_guard lock(mutex_);
            schedule_start = !host_auto_start_pending_;
            host_auto_start_pending_ = false;
        }
        if (schedule_start) {
            ble_hs_sched_start();
        }
#else
        ble_hs_sched_start();
#endif

        BROOKESIA_LOGI("NimBLE peripheral started");
        return true;
    }

    bool stop()
    {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

        {
            std::lock_guard lock(mutex_);
            if (!started_) {
                BROOKESIA_LOGD("BLE backend is not started, skip stop");
                return true;
            }
            shutting_down_ = true;
            advertising_requested_ = false;
        }

        bool success = stop_advertising_internal();

        auto connection_id = get_connection_id();
        if (connection_id.has_value()) {
            const auto result = ble_gap_terminate(connection_id.value(), BLE_ERR_REM_USER_CONN_TERM);
            if ((result != 0) && (result != BLE_HS_EALREADY) && (result != BLE_HS_ENOTCONN)) {
                emit_error("stop", result, "ble_gap_terminate failed");
                success = false;
            }
            for (int index = 0; (index < DISCONNECT_WAIT_ITERATIONS) && get_connection_id().has_value(); ++index) {
                vTaskDelay(pdMS_TO_TICKS(DISCONNECT_WAIT_INTERVAL_MS));
            }
        }

        const auto stop_result = nimble_port_stop();
        if (stop_result != 0) {
            emit_error("stop", stop_result, "nimble_port_stop failed");
            std::lock_guard lock(mutex_);
            shutting_down_ = false;
            return false;
        }

        const auto reset_result = ble_gatts_reset();
        if (reset_result != 0) {
            emit_error("stop", reset_result, "ble_gatts_reset failed");
            success = false;
        }
        reset_gatt_handles();

        {
            std::lock_guard lock(mutex_);
            connection_.reset();
            subscribed_handles_.clear();
            advertising_ = false;
            synced_ = false;
            started_ = false;
            shutting_down_ = false;
            gatt_needs_readd_ = true;
        }
        BROOKESIA_LOGI("NimBLE peripheral stopped");
        return success;
    }

    bool start_advertising()
    {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

        bool should_start = false;
        {
            std::lock_guard lock(mutex_);
            BROOKESIA_CHECK_FALSE_RETURN(started_, false, "BLE backend is not started");
            advertising_requested_ = true;
            should_start = synced_ && !connection_.has_value();
        }

        // Starting before host sync is accepted; on_stack_sync performs the actual GAP operation.
        return !should_start || start_advertising_internal();
    }

    bool stop_advertising()
    {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

        {
            std::lock_guard lock(mutex_);
            advertising_requested_ = false;
        }
        return stop_advertising_internal();
    }

    std::vector<bluetooth::ble::ConnectionInfo> get_connections() const
    {
        std::lock_guard lock(mutex_);
        if (!connection_.has_value()) {
            return {};
        }
        return {connection_.value()};
    }

    bool is_subscribed(uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic) const
    {
        const auto *runtime = find_characteristic(characteristic);
        if (runtime == nullptr) {
            return false;
        }

        std::lock_guard lock(mutex_);
        return connection_.has_value() && (connection_->connection_id == connection_id) &&
               subscribed_handles_.contains(runtime->value_handle);
    }

    bool notify(uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic, const bluetooth::ble::ByteArray &data)
    {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
        BROOKESIA_LOGD(
            "Params: connection_id(%1%), characteristic(%2%), size(%3%)", connection_id,
            BROOKESIA_DESCRIBE_TO_STR(characteristic), data.size()
        );

        const auto *runtime = find_characteristic(characteristic);
        if ((runtime == nullptr) || !runtime->config.notify || (runtime->value_handle == 0)) {
            emit_error("notify", BLE_HS_EINVAL, "Characteristic is not notify-capable");
            return false;
        }

        uint16_t mtu = bluetooth::ble::ATT_MTU_MIN;
        int validation_error = 0;
        std::string validation_message;
        {
            std::lock_guard lock(mutex_);
            if (!connection_.has_value() || (connection_->connection_id != connection_id)) {
                validation_error = BLE_HS_ENOTCONN;
                validation_message = "Connection does not exist";
            } else if (!subscribed_handles_.contains(runtime->value_handle)) {
                validation_error = BLE_HS_EDISABLED;
                validation_message = "Characteristic notification is not subscribed";
            } else {
                mtu = connection_->mtu;
            }
        }
        if (validation_error != 0) {
            emit_error("notify", validation_error, validation_message);
            return false;
        }

        if (data.size() > static_cast<size_t>(mtu - 3)) {
            emit_error("notify", BLE_HS_EMSGSIZE, "Notification exceeds the current ATT payload size");
            return false;
        }

        auto *buffer = ble_hs_mbuf_from_flat(data.data(), static_cast<uint16_t>(data.size()));
        if (buffer == nullptr) {
            emit_error("notify", BLE_HS_ENOMEM, "Failed to allocate notification buffer");
            return false;
        }

        const auto result = ble_gatts_notify_custom(connection_id, runtime->value_handle, buffer);
        if (result != 0) {
            // NimBLE owns the mbuf on all enabled-notification paths, including errors.
            emit_error("notify", result, "ble_gatts_notify_custom failed");
            return false;
        }
        return true;
    }

    bool disconnect(uint16_t connection_id)
    {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
        BROOKESIA_LOGD("Params: connection_id(%1%)", connection_id);

        bool connection_exists = false;
        {
            std::lock_guard lock(mutex_);
            connection_exists = connection_.has_value() && (connection_->connection_id == connection_id);
        }
        if (!connection_exists) {
            emit_error("disconnect", BLE_HS_ENOTCONN, "Connection does not exist");
            return false;
        }

        const auto result = ble_gap_terminate(connection_id, BLE_ERR_REM_USER_CONN_TERM);
        if ((result != 0) && (result != BLE_HS_EALREADY)) {
            emit_error("disconnect", result, "ble_gap_terminate failed");
            return false;
        }
        return true;
    }

private:
    bool prepare_controller()
    {
#if CONFIG_BT_CONTROLLER_DISABLED
        auto result = esp_hosted_connect_to_slave();
        if (result != ESP_OK) {
            emit_error("init", result, "ESP-Hosted transport or co-processor is unavailable");
            return false;
        }

        result = esp_hosted_bt_controller_init();
        if (result != ESP_OK) {
            emit_error("init", result, "ESP-Hosted co-processor does not provide a BLE controller");
            return false;
        }
        hosted_controller_initialized_ = true;

        result = esp_hosted_bt_controller_enable();
        if (result != ESP_OK) {
            emit_error("init", result, "Failed to enable the ESP-Hosted BLE controller");
            release_controller();
            return false;
        }
        hosted_controller_enabled_ = true;
#endif
        return true;
    }

    bool release_controller()
    {
        bool success = true;
#if CONFIG_BT_CONTROLLER_DISABLED
        if (hosted_controller_enabled_) {
            const auto result = esp_hosted_bt_controller_disable();
            if (result != ESP_OK) {
                emit_error("deinit", result, "Failed to disable the ESP-Hosted BLE controller");
                success = false;
            }
            hosted_controller_enabled_ = false;
        }
        if (hosted_controller_initialized_) {
            // Keep controller memory reusable for the next lazy init and leave the shared SDIO transport running.
            const auto result = esp_hosted_bt_controller_deinit(false);
            if (result != ESP_OK) {
                emit_error("deinit", result, "Failed to deinitialize the ESP-Hosted BLE controller");
                success = false;
            }
            hosted_controller_initialized_ = false;
        }
#endif
        return success;
    }

    bool build_gatt_database()
    {
        services_.clear();
        service_definitions_.clear();
        advertised_uuids_.clear();

        try {
            services_.reserve(config_.services.size());
            for (const auto &service_config : config_.services) {
                auto service = std::make_unique<ServiceRuntime>();
                service->uuid = parse_uuid(service_config.uuid);
                service->characteristics.reserve(service_config.characteristics.size());
                service->definitions.reserve(service_config.characteristics.size() + 1);

                for (const auto &characteristic_config : service_config.characteristics) {
                    auto characteristic = std::make_unique<CharacteristicRuntime>();
                    characteristic->owner = this;
                    characteristic->id = {
                        .service_uuid = service_config.uuid,
                        .characteristic_uuid = characteristic_config.uuid,
                    };
                    characteristic->config = characteristic_config;
                    characteristic->uuid = parse_uuid(characteristic_config.uuid);

                    ble_gatt_chr_flags flags = 0;
                    if (characteristic_config.write) {
                        flags |= BLE_GATT_CHR_F_WRITE;
                    }
                    if (characteristic_config.write_without_response) {
                        flags |= BLE_GATT_CHR_F_WRITE_NO_RSP;
                    }
                    if (characteristic_config.notify) {
                        flags |= BLE_GATT_CHR_F_NOTIFY;
                    }

                    auto *runtime = characteristic.get();
                    service->characteristics.emplace_back(std::move(characteristic));
                    ble_gatt_chr_def definition = {};
                    definition.uuid = &runtime->uuid.u;
                    definition.access_cb = on_gatt_access;
                    definition.arg = runtime;
                    definition.flags = flags;
                    definition.val_handle = &runtime->value_handle;
                    service->definitions.push_back(definition);
                }
                service->definitions.push_back({});
                services_.emplace_back(std::move(service));
            }

            service_definitions_.reserve(services_.size() + 1);
            for (const auto &service : services_) {
                ble_gatt_svc_def definition = {};
                definition.type = BLE_GATT_SVC_TYPE_PRIMARY;
                definition.uuid = &service->uuid.u;
                definition.characteristics = service->definitions.data();
                service_definitions_.push_back(definition);
            }
            service_definitions_.push_back({});

            advertised_uuids_.reserve(config_.advertised_service_uuids.size());
            for (const auto &uuid : config_.advertised_service_uuids) {
                advertised_uuids_.push_back(parse_uuid(uuid));
            }
        } catch (const std::exception &exception) {
            emit_error("init", BLE_HS_ENOMEM, exception.what());
            services_.clear();
            service_definitions_.clear();
            advertised_uuids_.clear();
            return false;
        }
        return true;
    }

    bool register_gatt_services()
    {
#if NIMBLE_BLE_CONNECT
        ble_svc_gap_init();
#endif
        ble_svc_gatt_init();

        const auto name_result = ble_svc_gap_device_name_set(config_.device_name.c_str());
        if (name_result != 0) {
            BROOKESIA_LOGW("Set GAP device name failed (%1%); advertising name remains available", name_result);
        }

        const auto count_result = ble_gatts_count_cfg(service_definitions_.data());
        if (count_result != 0) {
            emit_error("init", count_result, "ble_gatts_count_cfg failed");
            return false;
        }
        const auto add_result = ble_gatts_add_svcs(service_definitions_.data());
        if (add_result != 0) {
            emit_error("init", add_result, "ble_gatts_add_svcs failed");
            return false;
        }
        return true;
    }

    bool start_advertising_internal()
    {
        if (ble_gap_adv_active()) {
            set_advertising_state(true);
            return true;
        }

        if (advertised_uuids_.size() > std::numeric_limits<uint8_t>::max()) {
            emit_error("start_advertising", BLE_HS_EMSGSIZE, "Too many advertised service UUIDs");
            return false;
        }
        if (config_.device_name.size() > std::numeric_limits<uint8_t>::max()) {
            emit_error("start_advertising", BLE_HS_EMSGSIZE, "Device name is too long for NimBLE advertising");
            return false;
        }

        ble_hs_adv_fields fields = {};
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        if (!advertised_uuids_.empty()) {
            fields.uuids128 = advertised_uuids_.data();
            fields.num_uuids128 = static_cast<uint8_t>(advertised_uuids_.size());
            fields.uuids128_is_complete = 1;
        }
        auto result = ble_gap_adv_set_fields(&fields);
        if (result != 0) {
            emit_error("start_advertising", result, "Advertising service UUIDs do not fit in a legacy packet");
            return false;
        }

        ble_hs_adv_fields response = {};
        response.name = reinterpret_cast<uint8_t *>(config_.device_name.data());
        response.name_len = static_cast<uint8_t>(config_.device_name.size());
        response.name_is_complete = 1;
        result = ble_gap_adv_rsp_set_fields(&response);
        if (result != 0) {
            emit_error("start_advertising", result, "Device name does not fit in a legacy scan response");
            return false;
        }

        ble_gap_adv_params parameters = {};
        parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
        parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
        result = ble_gap_adv_start(
                     own_address_type_, nullptr, BLE_HS_FOREVER, &parameters, on_gap_event, this
                 );
        if ((result != 0) && (result != BLE_HS_EALREADY)) {
            emit_error("start_advertising", result, "ble_gap_adv_start failed");
            return false;
        }

        set_advertising_state(true);
        BROOKESIA_LOGI("BLE advertising started as '%1%'", config_.device_name);
        return true;
    }

    bool stop_advertising_internal()
    {
        if (!ble_gap_adv_active()) {
            set_advertising_state(false);
            return true;
        }

        const auto result = ble_gap_adv_stop();
        if ((result != 0) && (result != BLE_HS_EALREADY)) {
            emit_error("stop_advertising", result, "ble_gap_adv_stop failed");
            return false;
        }
        set_advertising_state(false);
        BROOKESIA_LOGI("BLE advertising stopped");
        return true;
    }

    void handle_stack_sync()
    {
        auto result = ble_hs_util_ensure_addr(0);
        if (result != 0) {
            emit_error("sync", result, "ble_hs_util_ensure_addr failed");
            return;
        }
        result = ble_hs_id_infer_auto(0, &own_address_type_);
        if (result != 0) {
            emit_error("sync", result, "ble_hs_id_infer_auto failed");
            return;
        }

        bool should_advertise = false;
        {
            std::lock_guard lock(mutex_);
            synced_ = true;
            should_advertise = started_ && advertising_requested_ && !connection_.has_value();
        }
        if (should_advertise) {
            start_advertising_internal();
        }
    }

    int handle_gap_event(ble_gap_event *event)
    {
        switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            handle_connection_event(event);
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            handle_disconnection_event(event);
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            handle_advertising_complete(event);
            return 0;
        case BLE_GAP_EVENT_MTU:
            handle_mtu_event(event);
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            handle_subscription_event(event);
            return 0;
        default:
            return 0;
        }
    }

    void handle_connection_event(const ble_gap_event *event)
    {
        set_advertising_state(false);
        if (event->connect.status != 0) {
            emit_error("connect", event->connect.status, "BLE connection attempt failed");
            bool should_restart = false;
            {
                std::lock_guard lock(mutex_);
                should_restart = advertising_requested_ && !shutting_down_;
            }
            if (should_restart) {
                start_advertising_internal();
            }
            return;
        }

        ble_gap_conn_desc descriptor = {};
        const auto find_result = ble_gap_conn_find(event->connect.conn_handle, &descriptor);
        bluetooth::ble::ConnectionInfo connection = {
            .connection_id = event->connect.conn_handle,
            .peer_address = find_result == 0 ? format_peer_address(descriptor.peer_id_addr) : std::string(),
            .mtu = ble_att_mtu(event->connect.conn_handle),
        };

        bluetooth::ble::PeripheralIface::Callbacks callbacks;
        {
            std::lock_guard lock(mutex_);
            connection_ = connection;
            subscribed_handles_.clear();
            callbacks = callbacks_;
        }
        if (callbacks.on_connection_state_changed) {
            callbacks.on_connection_state_changed(connection, true, {});
        }
        BROOKESIA_LOGI("BLE peer connected: %1%", connection.peer_address);
    }

    void handle_disconnection_event(const ble_gap_event *event)
    {
        bluetooth::ble::ConnectionInfo connection = {
            .connection_id = event->disconnect.conn.conn_handle,
            .peer_address = format_peer_address(event->disconnect.conn.peer_id_addr),
            .mtu = bluetooth::ble::ATT_MTU_MIN,
        };
        bluetooth::ble::PeripheralIface::Callbacks callbacks;
        bool should_restart = false;
        {
            std::lock_guard lock(mutex_);
            if (connection_.has_value()) {
                connection = connection_.value();
            }
            connection_.reset();
            subscribed_handles_.clear();
            callbacks = callbacks_;
            should_restart = started_ && advertising_requested_ && config_.auto_restart_advertising && !shutting_down_;
        }

        if (callbacks.on_connection_state_changed) {
            callbacks.on_connection_state_changed(
                connection, false, "reason=" + std::to_string(event->disconnect.reason)
            );
        }
        BROOKESIA_LOGI("BLE peer disconnected, reason=%1%", event->disconnect.reason);
        if (should_restart) {
            start_advertising_internal();
        }
    }

    void handle_advertising_complete(const ble_gap_event *event)
    {
        set_advertising_state(false);
        bool should_restart = false;
        {
            std::lock_guard lock(mutex_);
            // reason=0 normally means advertising ended because a connection is being established.
            should_restart = (event->adv_complete.reason != 0) && started_ && advertising_requested_ &&
                             !connection_.has_value() && !shutting_down_;
        }
        if (should_restart) {
            start_advertising_internal();
        }
    }

    void handle_mtu_event(const ble_gap_event *event)
    {
        bluetooth::ble::PeripheralIface::Callbacks callbacks;
        {
            std::lock_guard lock(mutex_);
            if (connection_.has_value() && (connection_->connection_id == event->mtu.conn_handle)) {
                connection_->mtu = event->mtu.value;
            }
            callbacks = callbacks_;
        }
        if (callbacks.on_mtu_changed) {
            callbacks.on_mtu_changed(event->mtu.conn_handle, event->mtu.value);
        }
    }

    void handle_subscription_event(const ble_gap_event *event)
    {
        const auto *runtime = find_characteristic(event->subscribe.attr_handle);
        if ((runtime == nullptr) || !runtime->config.notify) {
            return;
        }

        const bool subscribed = event->subscribe.cur_notify != 0;
        bluetooth::ble::PeripheralIface::Callbacks callbacks;
        {
            std::lock_guard lock(mutex_);
            if (subscribed) {
                subscribed_handles_.insert(runtime->value_handle);
            } else {
                subscribed_handles_.erase(runtime->value_handle);
            }
            callbacks = callbacks_;
        }
        if (callbacks.on_subscription_changed) {
            callbacks.on_subscription_changed(event->subscribe.conn_handle, runtime->id, subscribed);
        }
    }

    int handle_gatt_access(
        CharacteristicRuntime &runtime, uint16_t connection_id, const ble_gatt_access_ctxt *context
    )
    {
        if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        }

        const auto length = static_cast<uint16_t>(OS_MBUF_PKTLEN(context->om));
        bluetooth::ble::ByteArray data(length);
        if ((length > 0) && (os_mbuf_copydata(context->om, 0, length, data.data()) != 0)) {
            emit_error("gatt_write", BLE_HS_EUNKNOWN, "Failed to flatten NimBLE mbuf");
            return BLE_ATT_ERR_UNLIKELY;
        }

        bluetooth::ble::PeripheralIface::Callbacks callbacks;
        {
            std::lock_guard lock(mutex_);
            callbacks = callbacks_;
        }
        if (callbacks.on_characteristic_written) {
            callbacks.on_characteristic_written({
                .connection_id = connection_id,
                .characteristic = runtime.id,
                .data = std::move(data),
            });
        }
        return 0;
    }

    void set_advertising_state(bool advertising)
    {
        bluetooth::ble::PeripheralIface::Callbacks callbacks;
        bool changed = false;
        {
            std::lock_guard lock(mutex_);
            changed = advertising_ != advertising;
            advertising_ = advertising;
            callbacks = callbacks_;
        }
        if (changed && callbacks.on_advertising_state_changed) {
            callbacks.on_advertising_state_changed(advertising);
        }
    }

    void emit_error(const std::string &operation, int code, const std::string &message) const
    {
        bluetooth::ble::PeripheralIface::Callbacks callbacks;
        {
            std::lock_guard lock(mutex_);
            callbacks = callbacks_;
        }
        BROOKESIA_LOGE("BLE %1% failed: code(%2%), message(%3%)", operation, code, message);
        if (callbacks.on_error) {
            callbacks.on_error(operation, code, message);
        }
    }

    std::optional<uint16_t> get_connection_id() const
    {
        std::lock_guard lock(mutex_);
        if (!connection_.has_value()) {
            return std::nullopt;
        }
        return connection_->connection_id;
    }

    CharacteristicRuntime *find_characteristic(uint16_t value_handle)
    {
        for (const auto &service : services_) {
            for (const auto &characteristic : service->characteristics) {
                if (characteristic->value_handle == value_handle) {
                    return characteristic.get();
                }
            }
        }
        return nullptr;
    }

    const CharacteristicRuntime *find_characteristic(const bluetooth::ble::CharacteristicId &id) const
    {
        const bluetooth::ble::CharacteristicId normalized = {
            .service_uuid = bluetooth::ble::normalize_uuid(id.service_uuid),
            .characteristic_uuid = bluetooth::ble::normalize_uuid(id.characteristic_uuid),
        };
        if (normalized.service_uuid.empty() || normalized.characteristic_uuid.empty()) {
            return nullptr;
        }

        for (const auto &service : services_) {
            for (const auto &characteristic : service->characteristics) {
                if (characteristic->id == normalized) {
                    return characteristic.get();
                }
            }
        }
        return nullptr;
    }

    void reset_gatt_handles()
    {
        for (const auto &service : services_) {
            for (const auto &characteristic : service->characteristics) {
                characteristic->value_handle = 0;
            }
        }
    }

    void clear_runtime_after_deinit()
    {
        std::lock_guard lock(mutex_);
        initialized_ = false;
        started_ = false;
        synced_ = false;
        shutting_down_ = false;
        advertising_requested_ = false;
        advertising_ = false;
        gatt_needs_readd_ = false;
        connection_.reset();
        subscribed_handles_.clear();
        services_.clear();
        service_definitions_.clear();
        advertised_uuids_.clear();
        active_instance_ = nullptr;
        host_token_.reset();
#if MYNEWT_VAL(BLE_HS_AUTO_START)
        host_auto_start_pending_ = false;
#endif
    }

    static void on_stack_reset(int reason)
    {
        if (active_instance_ != nullptr) {
            active_instance_->emit_error("stack_reset", reason, "NimBLE host reset");
        }
    }

    static void on_stack_sync()
    {
        if (active_instance_ != nullptr) {
            active_instance_->handle_stack_sync();
        }
    }

    static void on_gatt_registered(ble_gatt_register_ctxt *context, void *)
    {
        [[maybe_unused]] char uuid[BLE_UUID_STR_LEN] = {};
        switch (context->op) {
        case BLE_GATT_REGISTER_OP_SVC:
            BROOKESIA_LOGD(
                "Registered BLE service %1%, handle(%2%)",
                ble_uuid_to_str(context->svc.svc_def->uuid, uuid), context->svc.handle
            );
            break;
        case BLE_GATT_REGISTER_OP_CHR:
            BROOKESIA_LOGD(
                "Registered BLE characteristic %1%, value_handle(%2%)",
                ble_uuid_to_str(context->chr.chr_def->uuid, uuid), context->chr.val_handle
            );
            break;
        default:
            break;
        }
    }

    static int on_gap_event(ble_gap_event *event, void *argument)
    {
        auto *backend = static_cast<Impl *>(argument);
        return backend != nullptr ? backend->handle_gap_event(event) : 0;
    }

    static int on_gatt_access(
        uint16_t connection_id, uint16_t, ble_gatt_access_ctxt *context, void *argument
    )
    {
        auto *runtime = static_cast<CharacteristicRuntime *>(argument);
        if ((runtime == nullptr) || (runtime->owner == nullptr)) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        return runtime->owner->handle_gatt_access(*runtime, connection_id, context);
    }

    static void host_task(void *)
    {
        nimble_port_run();
        vTaskDelete(nullptr);
    }

    inline static Impl *active_instance_ = nullptr;

    mutable std::mutex mutex_;
    bluetooth::ble::PeripheralConfig config_;
    bluetooth::ble::PeripheralIface::Callbacks callbacks_;
    bool configured_ = false;
    bool initialized_ = false;
    bool started_ = false;
    bool synced_ = false;
    bool shutting_down_ = false;
    bool advertising_requested_ = false;
    bool advertising_ = false;
    bool gatt_needs_readd_ = false;
#if CONFIG_BT_CONTROLLER_DISABLED
    bool hosted_controller_initialized_ = false;
    bool hosted_controller_enabled_ = false;
#endif
#if MYNEWT_VAL(BLE_HS_AUTO_START)
    bool host_auto_start_pending_ = false;
#endif
    uint8_t own_address_type_ = 0;
    std::optional<bluetooth::ble::ConnectionInfo> connection_;
    std::unordered_set<uint16_t> subscribed_handles_;
    std::vector<std::unique_ptr<ServiceRuntime>> services_;
    std::vector<ble_gatt_svc_def> service_definitions_;
    std::vector<ble_uuid128_t> advertised_uuids_;
    bluetooth::detail::BluetoothHostCoordinator::Token host_token_;
};

BleEspBackend::BleEspBackend()
    : impl_(std::make_unique<Impl>())
{
}

BleEspBackend::~BleEspBackend() = default;

bool BleEspBackend::configure(const bluetooth::ble::PeripheralConfig &config, bluetooth::ble::PeripheralIface::Callbacks callbacks)
{
    return impl_->configure(config, std::move(callbacks));
}

bool BleEspBackend::clear_callbacks()
{
    return impl_->clear_callbacks();
}

bool BleEspBackend::init()
{
    return impl_->init();
}

bool BleEspBackend::deinit()
{
    return impl_->deinit();
}

bool BleEspBackend::start()
{
    return impl_->start();
}

bool BleEspBackend::stop()
{
    return impl_->stop();
}

bool BleEspBackend::start_advertising()
{
    return impl_->start_advertising();
}

bool BleEspBackend::stop_advertising()
{
    return impl_->stop_advertising();
}

std::vector<bluetooth::ble::ConnectionInfo> BleEspBackend::get_connections() const
{
    return impl_->get_connections();
}

bool BleEspBackend::is_subscribed(
    uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic
) const
{
    return impl_->is_subscribed(connection_id, characteristic);
}

bool BleEspBackend::notify(
    uint16_t connection_id, const bluetooth::ble::CharacteristicId &characteristic, const bluetooth::ble::ByteArray &data
)
{
    return impl_->notify(connection_id, characteristic, data);
}

bool BleEspBackend::disconnect(uint16_t connection_id)
{
    return impl_->disconnect(connection_id);
}

} // namespace esp_brookesia::hal
