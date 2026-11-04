# ESP-Brookesia BLE 服务

* [English Version](./README.md)

## 概述

`brookesia_service_ble` 通过 ESP-Brookesia 服务框架提供运行时可配置、单连接的 BLE
Peripheral/GATT Server。服务本身与平台无关：ESP 使用 NimBLE HAL 实现，Linux 可使用 BlueZ 或确定性 stub，
WebAssembly 使用内存仿真后端。

0.8.0 版本支持 128-bit UUID、Write、Write Without Response、Notify、MTU 上报、
连接控制和广播控制。
当前版本明确不提供 BLE Central、经典蓝牙、GATT Read/Indicate、配对、加密、Bonding 或持久化密钥。

## 配置约定

`SetPeripheralConfig` 接受 described 类型 `hal::bluetooth::ble::PeripheralConfig`：

| 字段 | 约束 |
| --- | --- |
| `device_name` | 非空广播设备名 |
| `preferred_mtu` | 23 到 527，默认 247 |
| `max_connections` | 必须为 1 |
| `auto_restart_advertising` | 启用后在断连时自动恢复广播 |
| `advertised_service_uuids` | 放入广播数据的规范 128-bit UUID |
| `services` | 运行时 GATT 服务与特征配置 |

所有服务和特征 UUID 都必须使用规范的 `8-4-4-4-12` 格式。UUID 匹配不区分大小写，
合法输入会统一转换为小写。服务 UUID、同一服务中的特征 UUID 以及广播服务 UUID 均不能重复。
每个特征至少需要启用 `write`、
`write_without_response` 或 `notify` 中的一项。

特征必须同时通过服务 UUID 和特征 UUID 定位，
因此相同的特征 UUID 可以安全地出现在不同服务中。

## 服务 API

服务名固定为 `Ble`。

Functions：

- `SetPeripheralConfig(Config)`
- `GetPeripheralConfig()`
- `TriggerAdvertisingStart()` 和 `TriggerAdvertisingStop()`
- `GetState()` 和 `GetConnections()`
- `Notify(ConnectionId, ServiceUuid, CharacteristicUuid, Data)`
- `Disconnect(ConnectionId)`

Events：

- `GeneralStateChanged(GeneralState)`
- `AdvertisingStateChanged(IsAdvertising)`
- `ConnectionStateChanged(Connection, IsConnected, Reason)`
- `MtuChanged(ConnectionId, Mtu)`
- `SubscriptionChanged(ConnectionId, ServiceUuid, CharacteristicUuid, NotifyEnabled)`
- `CharacteristicWritten(ConnectionId, ServiceUuid, CharacteristicUuid, Data)`
- `ErrorHappened(Operation, Code, Message)`

`Data` 始终是 JSON/JavaScript 字节数组，每项必须为 0 到 255 的整数；二进制数据不能使用 `RawBuffer`。
当连接不存在、客户端未订阅、目标特征不支持 Notify，
或数据超过当前 `MTU - 3` 时，通知操作会失败。

## 生命周期

绑定服务后只进入 `Ready`，不会自动开始广播。应用应先配置 GATT，
再调用 `TriggerAdvertisingStart`；BLE host 会在首次请求广播时延迟初始化。
停止广播不会断开现有客户端连接。

仅在广播停止且没有客户端连接时才允许 `SetPeripheralConfig`。若 host 已经启动，
重配置会先安全停止并反初始化 host，再保存新配置。
释放最后一个服务绑定时按广播、连接、host、回调、HAL 所有权的逆序清理。

通用状态包括 `Idle`、`Ready`、`Starting`、`Started`、`Stopping` 和 `Error`。

## C++ 示例

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

应用必须初始化并启动 `ServiceManager`，绑定 `Ble`，并在使用 helper 期间持有该 binding。为避免遗漏初始状态变化，
应在开始广播前订阅事件。

## 构建选项

- ESP-IDF：`CONFIG_BROOKESIA_SERVICE_BLE_ENABLE_AUTO_REGISTER` 和
  `CONFIG_BROOKESIA_SERVICE_BLE_ENABLE_DEBUG_LOG`。
- PC：`BROOKESIA_SERVICE_BLE_ENABLE_AUTO_REGISTER` 和
  `BROOKESIA_SERVICE_BLE_PC_CONFIG_ENABLE_DEBUG_LOG`。
- Native 与 WebAssembly 使用方链接 `brookesia::service_ble`。

具体真机或仿真后端由 HAL 组件选择。
Linux 通过 `BROOKESIA_HAL_LINUX_BLE_BACKEND=auto|bluez|stub` 选择后端；WebAssembly 仅提供仿真，
因为浏览器 Web Bluetooth 不开放 Peripheral/GATT Server 能力。

## 测试

`test_apps` 同时提供确定性的 ESP-IDF/Unity 和 PC/Boost.Test 测试，覆盖 schema、校验、二进制数据、
插件发现、生命周期重启、事件、定向通知和失败清理。
构建命令见 [test_apps/README.md](./test_apps/README.md)。
