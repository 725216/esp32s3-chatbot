# ESP-Brookesia 蓝牙音箱服务

`brookesia_service_bt_speaker` 提供高层 `BtSpeaker` 媒体服务。它负责 A2DP 音箱策略，并将蓝牙 Host、
A2DP/AVRCP、PCM 解码和 AudioPlayback DataFlow 路由委托给 `brookesia_service_bt`。

应用先绑定 `BtSpeaker`，调用 `SetConfig`，再显式调用 `Start`。仅建立服务绑定不会自动启动 A2DP。
服务提供连接、流、播放状态、元数据和音量状态，以及暂停、恢复、上下曲、音量和断开连接控制。

`BtSpeaker` 持有 profile 期间，其他消费者不应直接调用 `Bt` 的 A2DP Functions；`BtSpeaker` 是该
profile 唯一的高层控制入口。

启用 `stop_local_playback_on_connect` 后，蓝牙连接成功时会异步停止已存在的本地
`AudioPlayback` 服务。本地音频服务不存在时不会影响蓝牙连接。

不支持经典蓝牙的目标仍可发现并绑定该服务，但 `Start` 会返回明确的 A2DP 不可用错误。
BLE GATT 继续由独立的 `brookesia_service_ble` 提供。

## 配置

使用 `CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_AUTO_REGISTER` 启用
自动注册；使用 `CONFIG_BROOKESIA_SERVICE_BT_SPEAKER_ENABLE_DEBUG_LOG`
控制调试日志。
