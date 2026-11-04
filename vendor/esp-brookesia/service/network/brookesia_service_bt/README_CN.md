# ESP-Brookesia Bluetooth 服务

`brookesia_service_bt` 提供通用 `Bt` 服务，首版支持单连接 Classic Bluetooth
A2DP Sink 和 AVRCP 控制，解码后的 PCM 通过 Manager DataFlow AudioPlayback
输出。

BLE GATT 仍由 `brookesia_service_ble` 提供；支持双模 Bluetooth 的目标由两个
服务共享底层 Bluetooth host。

Linux 和 WASM 使用确定性的内存后端，用于测试和模拟器集成。ESP 目标若仅
支持 BLE 或使用 ESP-Hosted，则会保留 BLE 能力，并明确报告 A2DP/AVRCP 不可用；
只有选择提供 Classic Bluetooth 后端的板级适配器后才会启用音箱能力，不会在硬件
不支持 Classic 时静默伪造无线音箱。
