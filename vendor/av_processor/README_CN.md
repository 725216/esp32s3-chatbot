# AV Processor 组件（音视频处理）

`av_processor` 是一个面向应用层的音视频处理组件，用来把 ESP 平台上常见的音频与视频中间件能力整理成更统一、更容易集成的一组模块。

它构建在 GMF、`esp_audio_simple_player` 等基础能力之上；视频采集与编解码栈（`esp_capture`、`esp_video_codec`）可按需启用。适合需要录音、播放、可选视频采集以及调试导出的项目。

## 平台支持

- **ESP-IDF**：v6.0 及以上（已在 GCC 15 默认工具链下验证）
- **芯片**：ESP32、ESP32-S2、ESP32-S3、ESP32-C3/C5/C6、ESP32-P4、**ESP32-S31** 以及底层 GMF/音频依赖所支持的其他目标

## 文档导航

本 `README_CN.md` 只保留组件概览、能力说明和示例入口。

如果你要查看完整 API、配置结构体、生命周期、模式差异、使用流程和注意事项，请直接跳转到下面这些文档：

- 详细使用指南（含完整 API 索引）：[docs/COMPONENT_GUIDE_CN.md](docs/COMPONENT_GUIDE_CN.md)
- 音频 API 头文件：[include/audio_processor.h](include/audio_processor.h)
- 视频 API 头文件：[include/video_processor.h](include/video_processor.h)
- 公共类型定义：[include/av_processor_type.h](include/av_processor_type.h)

## 组件能力概览

### 音频能力

- 音频管理器初始化与板级输入输出抽象
- 音频录制，基于 `audio_recorder_handle_t` 管理实例
- 录音链路前端处理
- 录音输出编码，支持 PCM、G711、OPUS、AAC、MP3 等格式
- 统一音频播放接口，基于 `audio_play_handle_t` 管理实例，支持 URL 或本地文件
- Feeder 喂数播放，基于 `audio_feeder_handle_t` 管理实例，适合 RTC、网络流、私有协议接收后本地播放
- 提示音播放通过同一套 `audio_play_*` API 实现，适合短音频、阻塞式、高优先级场景
- 可选混音器，以及播放流和 feeder 流之间的焦点/渐变控制

### 前端与算法能力

- 内置 `ai_afe` 路径，可直接使用 `esp-sr` 中整套 AFE 能力
- custom frontend 路径，面向按模块组合 `esp-sr` 独立能力的接入方式
- 支持按需启用 AEC、NS、VAD、AGC、Wakeup、Voice Command Detection 等能力
- 当前 custom 路径已内置支持独立 WakeNet 场景

## Kconfig 配置项

在 `menuconfig` 的 **Component config → Audio/Video Processor Configuration** 中配置，或在工程的 `sdkconfig.defaults` 里写入对应选项。

| 选项 | 说明 |
|------|------|
| `CONFIG_AUDIO_PROCESSOR_IMPL_GMF` | GMF 管线实现（默认）。`audio_manager_init()` 会注册默认编解码器并调用 `media_lib_add_default_adapter()`；按条件拉取 `media_lib_sal`、`esp_hls_stream` 等 managed 依赖。 |
| `CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT` | 轻量 recorder/feeder，保留播放链路，使用 `audio_processor_light.c`，适合更低内存占用。 |
| `CONFIG_VIDEO_PROCESSOR_ENABLE` | 编译视频采集/渲染（`video_processor.c`）并拉取 `esp_capture`、`esp_video_codec`。ESP32-S3/P4 默认开启，其他芯片默认关闭；纯音频工程请设为 `n`。 |
| `CONFIG_AUDIO_AFE_ENABLE` | 录音链路 AFE。ESP32、ESP32-S3、ESP32-P4、ESP32-S31 默认开启。 |
| `CONFIG_AUDIO_HLS_ENABLE` | HLS 播放（仅 GMF 实现）。 |
| `CONFIG_MEDIA_DUMP_ENABLE` | 调试音频导出（SD 卡或 UDP）。 |

**纯音频示例**：[examples/audio_afe](examples/audio_afe) 在 `sdkconfig.defaults` 中设置 `CONFIG_VIDEO_PROCESSOR_ENABLE=n`，避免拉取视频依赖链。

**视频示例**：[examples/video_preview](examples/video_preview) 设置 `CONFIG_VIDEO_PROCESSOR_ENABLE=y`。

修改会影响 managed 依赖的 Kconfig 后，请先执行 `idf.py reconfigure` 或 `idf.py fullclean` 再编译。

## 轻量音频实现

针对内存更紧张的目标，组件还提供了 `CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT`，用于编译 [src/audio_processor_light.c](src/audio_processor_light.c)。

这个实现保留 `audio_manager_init()` 和现有播放链路，但把 recorder 和 feeder 改成更简单的单任务队列式实现，重点是降低内部 RAM 占用。

它主要面向没有 PSRAM 的芯片，例如 ESP32-C3。推荐从 [examples/audio_wn_play_rec](examples/audio_wn_play_rec) 入手，这个示例把唤醒、录音、feeder 播放和 flash 回环放在了一条低内存链路里。

### 视频能力（可选，`CONFIG_VIDEO_PROCESSOR_ENABLE`）

启用视频后，组件提供：

- 视频采集
- 多 sink 输出
- 流模式回调处理
- fetch 模式获取/释放帧
- 视频解码
- 解码后的渲染回调

### 调试与问题分析能力

- Media Dump 调试导出
- 支持将录音链路中的音频原始数据导出到 SD 卡或通过 UDP 发送
- 支持抓取 AEC 前或 AEC 后的数据，便于分析回声、回授、削顶、底噪和处理效果

## 适合的典型场景

- 设备端录音并上传到云端或本地存储
- 网络音频接收后边解码边播放
- 播放主音频流时插入高优先级提示音
- 摄像头采集后做本地预览、编码输出或多路分发
- 同时输出压缩视频流和原始视频流
- 复现现场问题后导出原始音频做离线分析

## 音频模块实例模型

当前音频 API 已统一为 handle 风格：

- `audio_recorder_open(..., &handle)` 创建录音实例
- `audio_feeder_open(..., &handle)` 创建喂数实例
- `audio_play_open(..., &handle)` 创建播放实例
- 通过 `audio_play_config_t.type` 区分普通播放和提示音播放

后续的 `pause`、`resume`、`read`、`feed`、`play`、`stop`、`close` 都需要显式传入对应 handle。

这意味着同一应用中可以同时持有多个实例，例如同时创建两个 recorder，或者同时存在 playback 类型和 prompt 类型的 play 实例。

## 示例工程

仓库中已经提供了几组比较典型的示例，建议优先从示例入手理解组件的整体使用方式。

### 音频示例

- [examples/audio_afe](examples/audio_afe)
  演示内置 `ai_afe` 路径（含 ESP32-S31 等目标），纯音频配置（`CONFIG_VIDEO_PROCESSOR_ENABLE=n`），适合查看 AEC、NS、VAD、AGC 等前端处理能力的接入方式。

- [examples/audio_sr_vc_switch](examples/audio_sr_vc_switch)
  演示在 `AFE_TYPE_SR` 和 `AFE_TYPE_VC` 两种录音前端之间切换，也适合参考多 recorder 实例和控制台切换流程。

- [examples/audio_wn](examples/audio_wn)
  演示 custom frontend 路径和独立 WakeNet 的使用方式，也适合参考录音事件回调的处理流程。

- [examples/audio_wn_play_rec](examples/audio_wn_play_rec)
  演示基于 `audio_processor_light.c` 的轻量实现，适合没有 PSRAM 的芯片，如 ESP32-C3，覆盖唤醒、录音和 feeder 播放场景。

- [examples/audio_echo](examples/audio_echo)
  演示音频播放和 feeder 相关能力，适合做播放链路、混音器和接收后播放场景的参考。

- [examples/audio_play_test](examples/audio_play_test)
  演示统一 `audio_play_*` API、普通播放与提示音播放、feeder 喂数、混音焦点切换以及控制台交互命令。

- [examples/audio_mem_leak](examples/audio_mem_leak)
  用于循环验证 recorder、play、feeder 等模块的打开/关闭过程，适合做内存占用和泄漏排查。

### 视频示例

需要 `CONFIG_VIDEO_PROCESSOR_ENABLE=y`。

- [examples/video_preview](examples/video_preview)
  演示视频采集与预览流程，适合快速理解 capture 的基本接法。

- [examples/video_test](examples/video_test)
  用于验证和测试视频链路相关能力，适合排查 sink、格式和处理链路问题。

## Media Dump 说明

Media Dump 是一个面向调试的辅助能力，不是常规业务 API。

目前它主要用于录音链路音频数据导出，可用于：

- 排查“设备播放的声音再次被麦克风收回去”的回授问题
- 评估 AEC 处理前后的差异
- 检查音频是否存在削顶、饱和、底噪偏大、格式不匹配等问题

当前支持：

- AEC 前抓取
- AEC 后抓取
- SD 卡文件输出
- UDP 实时发送

详细配置方式和注意事项请查看：

- Media Dump 详细说明：[docs/COMPONENT_GUIDE_CN.md](docs/COMPONENT_GUIDE_CN.md)

## 如何选择阅读路径

- 如果你只是想知道这个组件能做什么，以及应该从哪个示例开始，看本 README 就够了。
- 如果你已经准备接入代码，需要查看结构体、宏、API 调用顺序、完整 API 索引或模式差异，请直接看 [docs/COMPONENT_GUIDE_CN.md](docs/COMPONENT_GUIDE_CN.md)。
- 如果你要确认函数签名、枚举值和配置字段，请直接查看公共头文件：
  [include/audio_processor.h](include/audio_processor.h)、
  [include/video_processor.h](include/video_processor.h)、
  [include/av_processor_type.h](include/av_processor_type.h)

## 版本信息

当前版本：`v0.6.6`

## 许可证

本组件遵循 MIT 许可证，详见仓库中的 `LICENSE` 文件。

## 相关资源

- 问题反馈：https://github.com/espressif/esp-gmf/issues
