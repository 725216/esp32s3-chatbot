# ESP-Brookesia 显示服务

* [English Version](./README.md)

## 概述

`brookesia_service_display` 是用于 LCD 输出、触摸输入和显示源仲裁的显示服务组件。

更多信息请参考 [ESP-Brookesia 编程指南](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/service/display.html)。

## DataFlow 集成

该服务会注册显示 DataFlow provider，提供输出与源发现、活动源路由和帧提交能力。视频消费者可以使用与 provider 无关的显示操作，而无需依赖本组件的具体实现细节。

## 如何使用

### 开发环境要求

请参考以下文档：

- [ESP-Brookesia 编程指南 - 版本说明](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-versioning)
- [ESP-Brookesia 编程指南 - 开发环境搭建](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-dev-environment)

### 添加到工程

请参考 [ESP-Brookesia 编程指南 - 如何获取和使用组件](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-component-usage)。
