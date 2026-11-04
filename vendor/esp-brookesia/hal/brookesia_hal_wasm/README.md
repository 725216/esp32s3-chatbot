# Brookesia HAL WASM

`brookesia_hal_wasm` provides simulator implementations of Brookesia HAL
interfaces for WebAssembly builds.

## BLE Peripheral Simulation

Browsers expose Web Bluetooth as a Central/GATT Client API and cannot publish a
local Peripheral/GATT Server. The WASM BLE HAL therefore implements a
deterministic in-memory peripheral; it never claims to advertise over a real
radio.

The public `ble::wasm_test` hooks inject a remote connection, negotiated MTU,
notification subscription, characteristic write, and disconnect. Notifications
sent through `ble::PeripheralIface` can be retrieved with
`ble::wasm_test::take_notifications()`. This provides the same lifecycle,
single-connection, UUID, binary-payload, and MTU validation as the hardware HAL
while keeping browser tests deterministic.

Run its native host validation with:

```bash
cmake -S host_test -B host_test/build
cmake --build host_test/build
ctest --test-dir host_test/build --output-on-failure
```
