# Brookesia HAL WASM

`brookesia_hal_wasm` 为 WebAssembly 构建提供 Brookesia HAL 的模拟器实现。

## BLE Peripheral 仿真

浏览器 Web Bluetooth 仅提供 Central/GATT Client API，不能发布本地
Peripheral/GATT Server。因此 WASM BLE HAL 使用确定性的内存仿真，不会声称执行了真实
无线广播。

公开的 `ble::wasm_test` hook 可注入远端连接、协商 MTU、通知订阅、特征写入和断开。
通过 `ble::PeripheralIface` 发送的通知可由
`ble::wasm_test::take_notifications()` 读取。该实现与硬件 HAL 使用相同的生命周期、
单连接、UUID、二进制 payload 和 MTU 校验，同时保证浏览器测试确定性。

可通过以下命令运行 native host 验证：

```bash
cmake -S host_test -B host_test/build
cmake --build host_test/build
ctest --test-dir host_test/build --output-on-failure
```
