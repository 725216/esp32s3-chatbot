# ESP-Brookesia Bluetooth Service

`brookesia_service_bt` exposes a generic `Bt` service. The initial profile is a
single-connection Classic Bluetooth A2DP Sink with AVRCP controls. PCM output
is routed through the Manager DataFlow AudioPlayback operation.

BLE GATT remains provided by `brookesia_service_ble`; both profiles share the
platform Bluetooth host when the target supports dual-mode Bluetooth.

Linux and WASM use an in-memory deterministic backend for tests and simulator
integration. On an ESP target, a BLE-only or ESP-Hosted controller exposes BLE
capabilities but reports A2DP/AVRCP as unavailable until a Classic-capable
board adaptor is selected; the service never silently emulates a wireless
speaker on hardware that cannot provide Classic Bluetooth.
