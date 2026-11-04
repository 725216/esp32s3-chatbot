# ESP-Brookesia BLE Service

* [中文版本](./README_CN.md)

## Overview

`brookesia_service_ble` exposes a runtime-configurable, single-connection BLE Peripheral/GATT Server through the
ESP-Brookesia service framework. The service is platform-neutral: ESP uses the NimBLE HAL implementation, Linux can
use BlueZ or the deterministic stub, and WebAssembly uses the in-memory simulation backend.

Version 0.8.0 supports 128-bit UUIDs, writable characteristics, write-without-response, notifications, MTU reporting,
connection control, and advertising control. It intentionally does not provide a BLE Central, Classic Bluetooth,
GATT Read/Indicate, pairing, encryption, bonding, or persistent keys.

## Configuration Contract

`SetPeripheralConfig` accepts a described `hal::bluetooth::ble::PeripheralConfig` object:

| Field | Constraint |
| --- | --- |
| `device_name` | Non-empty advertising device name |
| `preferred_mtu` | 23 through 527; defaults to 247 |
| `max_connections` | Must be 1 |
| `auto_restart_advertising` | Restart advertising after disconnect when enabled |
| `advertised_service_uuids` | Canonical 128-bit UUIDs included in advertising |
| `services` | Runtime GATT services and characteristics |

Every service and characteristic UUID must use canonical `8-4-4-4-12` syntax. UUID matching is case-insensitive and
valid input is normalized to lowercase. Service UUIDs, characteristic UUIDs within the same service, and advertised
service UUIDs must not be duplicated. Each characteristic must enable at least one of `write`,
`write_without_response`, or `notify`.

A characteristic is addressed by both its service UUID and characteristic UUID. This permits the same characteristic
UUID to appear in different services without ambiguity.

## Service API

The service name is `Ble`.

Functions:

- `SetPeripheralConfig(Config)`
- `GetPeripheralConfig()`
- `TriggerAdvertisingStart()` and `TriggerAdvertisingStop()`
- `GetState()` and `GetConnections()`
- `Notify(ConnectionId, ServiceUuid, CharacteristicUuid, Data)`
- `Disconnect(ConnectionId)`

Events:

- `GeneralStateChanged(GeneralState)`
- `AdvertisingStateChanged(IsAdvertising)`
- `ConnectionStateChanged(Connection, IsConnected, Reason)`
- `MtuChanged(ConnectionId, Mtu)`
- `SubscriptionChanged(ConnectionId, ServiceUuid, CharacteristicUuid, NotifyEnabled)`
- `CharacteristicWritten(ConnectionId, ServiceUuid, CharacteristicUuid, Data)`
- `ErrorHappened(Operation, Code, Message)`

`Data` is always a JSON/JavaScript byte array. Every element must be an integer from 0 through 255; binary payloads
must not use `RawBuffer`. A notification fails if the connection does not exist, the client is not subscribed, the
target characteristic is not notifiable, or the payload exceeds the negotiated `MTU - 3` limit.

## Lifecycle

Binding the service leaves it in `Ready` and does not start advertising. Configure GATT first, then call
`TriggerAdvertisingStart`; the BLE host is initialized lazily on that first request. Stopping advertising does not
disconnect a connected client.

`SetPeripheralConfig` is accepted only while advertising is stopped and no client is connected. Reconfiguration
stops and deinitializes an already-started host before storing the new configuration. Releasing the final service
binding performs cleanup in reverse order: advertising, connections, host, callbacks, and HAL ownership.

The general state is one of `Idle`, `Ready`, `Starting`, `Started`, `Stopping`, or `Error`.

## C++ Example

```cpp
#include "brookesia/service_helper/network/ble.hpp"

using Ble = esp_brookesia::service::helper::Ble;

Ble::PeripheralConfig config{
    .device_name = "Brookesia-BLE",
    .preferred_mtu = 247,
    .max_connections = 1,
    .auto_restart_advertising = true,
    .advertised_service_uuids = {"7a5a0001-9b7b-4d20-8f30-6d9f0e7f4a10"},
    .services = {{
        .uuid = "7a5a0001-9b7b-4d20-8f30-6d9f0e7f4a10",
        .characteristics = {
            {.uuid = "7a5a0002-9b7b-4d20-8f30-6d9f0e7f4a10", .write = true},
            {.uuid = "7a5a0003-9b7b-4d20-8f30-6d9f0e7f4a10", .notify = true},
        },
    }},
};

auto set_result = Ble::call_function_sync(
    Ble::FunctionId::SetPeripheralConfig,
    BROOKESIA_DESCRIBE_TO_JSON(config).as_object()
);
if (set_result) {
    Ble::call_function_sync(Ble::FunctionId::TriggerAdvertisingStart);
}
```

The application must initialize and start `ServiceManager`, bind `Ble`, and keep that binding alive while using the
helper. Subscribe to events before starting advertising when no initial state transition may be missed.

## Build Options

- ESP-IDF: `CONFIG_BROOKESIA_SERVICE_BLE_ENABLE_AUTO_REGISTER` and
  `CONFIG_BROOKESIA_SERVICE_BLE_ENABLE_DEBUG_LOG`.
- PC: `BROOKESIA_SERVICE_BLE_ENABLE_AUTO_REGISTER` and
  `BROOKESIA_SERVICE_BLE_PC_CONFIG_ENABLE_DEBUG_LOG`.
- Native and WebAssembly consumers link `brookesia::service_ble`.

The concrete radio or simulation backend is selected by the HAL component. On Linux,
`BROOKESIA_HAL_LINUX_BLE_BACKEND=auto|bluez|stub` controls backend selection; WebAssembly is simulation-only because
browsers do not expose a Peripheral/GATT Server through Web Bluetooth.

## Tests

The `test_apps` directory provides deterministic ESP-IDF/Unity and PC/Boost.Test coverage for schemas, validation,
binary data, plugin discovery, lifecycle restart, events, directed notification, and failure cleanup. See
[test_apps/README.md](./test_apps/README.md) for commands.
