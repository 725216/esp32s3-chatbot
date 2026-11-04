# ESP-Brookesia Bluetooth Speaker Service

`brookesia_service_bt_speaker` provides the high-level `BtSpeaker` media service. It owns A2DP speaker policy while
delegating Bluetooth host, A2DP/AVRCP, PCM decoding, and AudioPlayback DataFlow routing to `brookesia_service_bt`.

Applications bind `BtSpeaker`, call `SetConfig`, and then call `Start`. Starting the service binding alone does not
start A2DP. The service exposes connection, stream, playback, metadata, and volume state plus pause, resume, track,
volume, and disconnect controls.

While `BtSpeaker` owns the profile, other consumers must not call the `Bt` A2DP functions directly. `BtSpeaker` is
the single high-level control entry for that profile.

When `stop_local_playback_on_connect` is enabled, a successful Bluetooth connection asynchronously stops the local
`AudioPlayback` service if it is present. Missing local audio support never prevents the Bluetooth connection.

Targets without Classic Bluetooth keep the service available for discovery, but `Start` returns an explicit A2DP
unavailable error. BLE GATT remains independent in `brookesia_service_ble`.

## Configuration

Enable automatic registration with `CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_AUTO_REGISTER`. Debug logs are
controlled by `CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_DEBUG_LOG`.
