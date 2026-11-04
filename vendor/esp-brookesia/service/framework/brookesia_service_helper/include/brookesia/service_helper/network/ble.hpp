/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <array>
#include <span>
#include <vector>

#include "brookesia/hal_interface/interfaces/bluetooth/ble/types.hpp"
#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_manager/detail/static_schema.hpp"
#include "brookesia/service_manager/helper/base.hpp"

namespace esp_brookesia::service::helper {

/** Helper schema definitions for the BLE peripheral service. */
class Ble: public Base<Ble> {
public:
    using CharacteristicId = hal::bluetooth::ble::CharacteristicId;
    using CharacteristicConfig = hal::bluetooth::ble::CharacteristicConfig;
    using ServiceConfig = hal::bluetooth::ble::ServiceConfig;
    using PeripheralConfig = hal::bluetooth::ble::PeripheralConfig;
    using ConnectionInfo = hal::bluetooth::ble::ConnectionInfo;

    enum class GeneralState { Idle, Ready, Starting, Started, Stopping, Error, Max };

    struct State {
        GeneralState general_state = GeneralState::Idle;
        bool is_configured = false;
        bool is_advertising = false;
        std::vector<ConnectionInfo> connections;
    };

    enum class FunctionId {
        SetPeripheralConfig, GetPeripheralConfig, TriggerAdvertisingStart, TriggerAdvertisingStop,
        GetState, GetConnections, Notify, Disconnect, Max,
    };

    enum class EventId {
        GeneralStateChanged, AdvertisingStateChanged, ConnectionStateChanged, MtuChanged,
        SubscriptionChanged, CharacteristicWritten, ErrorHappened, Max,
    };

    enum class FunctionSetPeripheralConfigParam { Config, Max };
    enum class FunctionNotifyParam { ConnectionId, ServiceUuid, CharacteristicUuid, Data, Max };
    enum class FunctionDisconnectParam { ConnectionId, Max };
    enum class EventGeneralStateChangedParam { GeneralState, Max };
    enum class EventAdvertisingStateChangedParam { IsAdvertising, Max };
    enum class EventConnectionStateChangedParam { Connection, IsConnected, Reason, Max };
    enum class EventMtuChangedParam { ConnectionId, Mtu, Max };
    enum class EventSubscriptionChangedParam { ConnectionId, ServiceUuid, CharacteristicUuid, NotifyEnabled, Max };
    enum class EventCharacteristicWrittenParam { ConnectionId, ServiceUuid, CharacteristicUuid, Data, Max };
    enum class EventErrorHappenedParam { Operation, Code, Message, Max };

private:
    using FunctionParameterSpec = detail::static_schema::FunctionParameterSpec;
    using FunctionReturnSpec = detail::static_schema::FunctionReturnSpec;
    using FunctionSpec = detail::static_schema::FunctionSpec;
    using EventItemSpec = detail::static_schema::EventItemSpec;
    using EventSpec = detail::static_schema::EventSpec;

    inline static constexpr std::array<FunctionParameterSpec, 1> CONFIG_PARAMETERS = {{
            {.name = "Config", .description = "BLE peripheral configuration object.", .type = FunctionValueType::Object},
        }
    };
    inline static constexpr std::array<FunctionParameterSpec, 4> NOTIFY_PARAMETERS = {{
            {.name = "ConnectionId", .description = "Opaque connection identifier.", .type = FunctionValueType::Number},
            {.name = "ServiceUuid", .description = "Canonical 128-bit service UUID.", .type = FunctionValueType::String},
            {.name = "CharacteristicUuid", .description = "Canonical 128-bit characteristic UUID.", .type = FunctionValueType::String},
            {.name = "Data", .description = "Owned byte array with values in the range 0..255.", .type = FunctionValueType::Array},
        }
    };
    inline static constexpr std::array<FunctionParameterSpec, 1> CONNECTION_PARAMETERS = {{
            {.name = "ConnectionId", .description = "Opaque connection identifier.", .type = FunctionValueType::Number},
        }
    };

    inline static constexpr std::array<FunctionSpec, static_cast<size_t>(FunctionId::Max)> FUNCTION_SPECS = {{
            {
                .name = "SetPeripheralConfig",
                .description = "Set the BLE peripheral and GATT server configuration.",
                .parameters = CONFIG_PARAMETERS,
            },
            {
                .name = "GetPeripheralConfig",
                .description = "Get the configured BLE peripheral and GATT profile.",
                .parameters = {},
                .return_value = FunctionReturnSpec{FunctionValueType::Object, "BLE peripheral configuration object."},
            },
            {
                .name = "TriggerAdvertisingStart",
                .description = "Initialize the BLE host when necessary and start advertising.",
                .parameters = {},
            },
            {.name = "TriggerAdvertisingStop", .description = "Stop advertising without disconnecting an existing client.", .parameters = {}},
            {
                .name = "GetState",
                .description = "Get the BLE host, advertising, and connection state.",
                .parameters = {},
                .return_value = FunctionReturnSpec{FunctionValueType::Object, "BLE peripheral state object."},
            },
            {
                .name = "GetConnections",
                .description = "Get current BLE client connections.",
                .parameters = {},
                .return_value = FunctionReturnSpec{FunctionValueType::Array, "Array of BLE connection objects."},
            },
            {.name = "Notify", .description = "Send a GATT notification to a subscribed client.", .parameters = NOTIFY_PARAMETERS},
            {.name = "Disconnect", .description = "Disconnect a BLE client.", .parameters = CONNECTION_PARAMETERS},
        }
    };
    static_assert(FUNCTION_SPECS.size() == static_cast<size_t>(FunctionId::Max));

    inline static constexpr std::array<EventItemSpec, 1> GENERAL_STATE_ITEMS = {{
            {"GeneralState", "Current BLE host lifecycle state.", EventItemType::String},
        }
    };
    inline static constexpr std::array<EventItemSpec, 1> ADVERTISING_ITEMS = {{
            {"IsAdvertising", "Whether advertising is active.", EventItemType::Boolean},
        }
    };
    inline static constexpr std::array<EventItemSpec, 3> CONNECTION_ITEMS = {{
            {"Connection", "Connection snapshot.", EventItemType::Object},
            {"IsConnected", "Whether the client is connected.", EventItemType::Boolean},
            {"Reason", "Backend-independent reason string.", EventItemType::String},
        }
    };
    inline static constexpr std::array<EventItemSpec, 2> MTU_ITEMS = {{
            {"ConnectionId", "Opaque connection identifier.", EventItemType::Number},
            {"Mtu", "Negotiated ATT MTU.", EventItemType::Number},
        }
    };
    inline static constexpr std::array<EventItemSpec, 4> SUBSCRIPTION_ITEMS = {{
            {"ConnectionId", "Opaque connection identifier.", EventItemType::Number},
            {"ServiceUuid", "Canonical 128-bit service UUID.", EventItemType::String},
            {"CharacteristicUuid", "Canonical 128-bit characteristic UUID.", EventItemType::String},
            {"NotifyEnabled", "Whether notifications are enabled.", EventItemType::Boolean},
        }
    };
    inline static constexpr std::array<EventItemSpec, 4> WRITTEN_ITEMS = {{
            {"ConnectionId", "Opaque connection identifier.", EventItemType::Number},
            {"ServiceUuid", "Canonical 128-bit service UUID.", EventItemType::String},
            {"CharacteristicUuid", "Canonical 128-bit characteristic UUID.", EventItemType::String},
            {"Data", "Owned byte array.", EventItemType::Array},
        }
    };
    inline static constexpr std::array<EventItemSpec, 3> ERROR_ITEMS = {{
            {"Operation", "Operation name.", EventItemType::String},
            {"Code", "Backend-independent error code.", EventItemType::Number},
            {"Message", "Diagnostic error message.", EventItemType::String},
        }
    };
    inline static constexpr std::array<EventSpec, static_cast<size_t>(EventId::Max)> EVENT_SPECS = {{
            {.name = "GeneralStateChanged", .description = "BLE host lifecycle state changed.", .items = GENERAL_STATE_ITEMS},
            {.name = "AdvertisingStateChanged", .description = "BLE advertising state changed.", .items = ADVERTISING_ITEMS},
            {.name = "ConnectionStateChanged", .description = "A BLE client connected or disconnected.", .items = CONNECTION_ITEMS},
            {.name = "MtuChanged", .description = "The negotiated ATT MTU changed.", .items = MTU_ITEMS},
            {.name = "SubscriptionChanged", .description = "A client enabled or disabled GATT notifications.", .items = SUBSCRIPTION_ITEMS},
            {.name = "CharacteristicWritten", .description = "A client wrote a GATT characteristic.", .items = WRITTEN_ITEMS},
            {.name = "ErrorHappened", .description = "A BLE backend operation failed.", .items = ERROR_ITEMS},
        }
    };
    static_assert(EVENT_SPECS.size() == static_cast<size_t>(EventId::Max));

public:
    static constexpr std::string_view get_name()
    {
        return "Ble";
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

BROOKESIA_DESCRIBE_ENUM(Ble::GeneralState, Idle, Ready, Starting, Started, Stopping, Error, Max);
BROOKESIA_DESCRIBE_ENUM(
    Ble::FunctionId, SetPeripheralConfig, GetPeripheralConfig, TriggerAdvertisingStart, TriggerAdvertisingStop,
    GetState, GetConnections, Notify, Disconnect, Max
);
BROOKESIA_DESCRIBE_ENUM(
    Ble::EventId, GeneralStateChanged, AdvertisingStateChanged, ConnectionStateChanged, MtuChanged,
    SubscriptionChanged, CharacteristicWritten, ErrorHappened, Max
);
BROOKESIA_DESCRIBE_ENUM(Ble::FunctionSetPeripheralConfigParam, Config, Max);
BROOKESIA_DESCRIBE_ENUM(Ble::FunctionNotifyParam, ConnectionId, ServiceUuid, CharacteristicUuid, Data, Max);
BROOKESIA_DESCRIBE_ENUM(Ble::FunctionDisconnectParam, ConnectionId, Max);
BROOKESIA_DESCRIBE_ENUM(Ble::EventGeneralStateChangedParam, GeneralState, Max);
BROOKESIA_DESCRIBE_ENUM(Ble::EventAdvertisingStateChangedParam, IsAdvertising, Max);
BROOKESIA_DESCRIBE_ENUM(Ble::EventConnectionStateChangedParam, Connection, IsConnected, Reason, Max);
BROOKESIA_DESCRIBE_ENUM(Ble::EventMtuChangedParam, ConnectionId, Mtu, Max);
BROOKESIA_DESCRIBE_ENUM(Ble::EventSubscriptionChangedParam, ConnectionId, ServiceUuid, CharacteristicUuid, NotifyEnabled, Max);
BROOKESIA_DESCRIBE_ENUM(Ble::EventCharacteristicWrittenParam, ConnectionId, ServiceUuid, CharacteristicUuid, Data, Max);
BROOKESIA_DESCRIBE_ENUM(Ble::EventErrorHappenedParam, Operation, Code, Message, Max);
BROOKESIA_DESCRIBE_STRUCT(Ble::State, (), (general_state, is_configured, is_advertising, connections));

} // namespace esp_brookesia::service::helper
