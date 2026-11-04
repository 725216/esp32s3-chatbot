# ESP-Brookesia Library Utils

* [English Version](./README.md)

## 概述

`brookesia_lib_utils` 是 ESP-Brookesia 框架的核心工具库，提供了一套完整的实用工具集，包括 `任务调度器`、`线程配置`、`性能分析工具`、`日志系统`、`状态机`、`插件系统`以及各种`辅助工具`。

更多信息请参考 [ESP-Brookesia 编程指南](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/utils/lib_utils/index.html)。

## 如何使用

### 开发环境要求

请参考以下文档：

 - [ESP-Brookesia 编程指南 - 版本说明](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-versioning)
 - [ESP-Brookesia 编程指南 - 开发环境搭建](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-dev-environment)

### 添加到工程

请参考 [ESP-Brookesia 编程指南 - 如何获取和使用组件](https://docs.espressif.com/projects/esp-brookesia/zh_CN/latest/getting_started.html#getting-started-component-usage)。

## 构建内存调优

组件被发现时会自动注册被动的 CMake 模块 `cmake/compile_tuning.cmake`。第一方 example、test app、host test 以及 PC simulator 在 target 创建完成后直接调用其命令，不需要通过相对路径定位模块；它通过 Ninja job pool 限制 C++ target 的并发编译，不改变运行时代码或 ABI。

| CMake 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `BROOKESIA_CXX_JOBS` | `6` | 选定 C++ target 的最大并发编译 edge 数量；设置为 `0` 可关闭 job pool |
| `BROOKESIA_FAST_COMPILE` | `OFF` | 为选定 C++ target 添加 `-g1` |
| `BROOKESIA_COMPILE_TUNING_INCLUDE_ESP_BOOST` | `ON` | target 存在时将 `esp-boost` 加入 job pool |

job pool 作用于 target，因此混合 C/C++ target 中的 C 源文件也会使用同一个 pool。GCC 下的 Brookesia target 还会关闭 IPA clone pass，减少每个翻译单元生成的 helper clone。旧的 `BROOKESIA_SUPER_CXX_JOBS` 和 `BROOKESIA_SUPER_FAST_COMPILE` 名称仍作为 System Super 的兼容别名接受。

对于包含高内存外部 ESP-IDF 组件的工程，可以显式传入其 build component 名称：

```cmake
brookesia_compile_tuning_apply_current_project(
    EXTERNAL_COMPONENTS espressif__esp-dl
)
```
