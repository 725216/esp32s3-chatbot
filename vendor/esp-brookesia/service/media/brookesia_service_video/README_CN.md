# ESP-Brookesia 视频服务

* [English Version](./README.md)

## 概述

`brookesia_service_video` 是用于视频编码、解码和帧传输的媒体服务组件。

更多信息请参考 [ESP-Brookesia 编程指南](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/service/video.html)。

## DataFlow 集成

配置显示 sink 后，编码器和解码器通过与 provider 无关的显示 DataFlow 操作发现输出、选择源并提交帧。视频服务不再需要直接依赖显示 provider 的具体实现。

## 如何使用

### 开发环境要求

请参考以下文档：

- [ESP-Brookesia 编程指南 - 版本说明](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-versioning)
- [ESP-Brookesia 编程指南 - 开发环境搭建](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-dev-environment)

### 添加到工程

请参考 [ESP-Brookesia 编程指南 - 如何获取和使用组件](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-component-usage)。
