# ESP-Brookesia Service Manager

* [English Version](./README.md)

## 概述

`brookesia_service_manager` 是 ESP-Brookesia 的服务生命周期、本地调用和事件框架。

更多信息请参考 [ESP-Brookesia 编程指南](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/service/framework/manager/index.html)。

## DataFlow 集成

内置 `DataFlow` 服务负责发现 provider 提供的显示、音频播放和音频采集操作。应用可以列出 provider 与输出、创建绑定到自身的操作并释放操作，而不需要依赖具体的媒体 provider 实现。

原生视频帧、PCM 和映射缓冲区访问仍通过类型安全的 C++ 操作接口提供；service helper 提供控制面的 schema 与路由操作。

## 如何使用

### 开发环境要求

请参考以下文档：

- [ESP-Brookesia 编程指南 - 版本说明](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-versioning)
- [ESP-Brookesia 编程指南 - 开发环境搭建](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-dev-environment)

### 添加到工程

请参考 [ESP-Brookesia 编程指南 - 如何获取和使用组件](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-component-usage)。
