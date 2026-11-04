# ESP-Brookesia Library Utils

* [中文版本](./README_CN.md)

## Overview

`brookesia_lib_utils` is the core utility library of the ESP-Brookesia framework, providing a comprehensive set of practical tools including `task scheduler`, `thread configuration`, `performance profilers`, `logging system`, `state machine`, `plugin system`, and various `helper utilities`.

For more information, see the [ESP-Brookesia Programming Guide](https://docs.espressif.com/projects/esp-brookesia/en/latest/utils/lib_utils/index.html).

## How to Use

### Environment Requirements

Please refer to the following documentation:

- [ESP-Brookesia Programming Guide - Versioning](https://docs.espressif.com/projects/esp-brookesia/en/latest/getting_started.html#getting-started-versioning)
- [ESP-Brookesia Programming Guide - Development Environment Setup](https://docs.espressif.com/projects/esp-brookesia/en/latest/getting_started.html#getting-started-dev-environment)

### Add to Your Project

Please refer to [ESP-Brookesia Programming Guide - How to Obtain and Use Components](https://docs.espressif.com/projects/esp-brookesia/en/latest/getting_started.html#getting-started-component-usage).

## Build Memory Tuning

The component registers the passive CMake module at `cmake/compile_tuning.cmake` when it is discovered. First-party examples, test apps, host tests, and the PC simulator call its commands after their targets are created; they do not need to locate the module with a relative path. It limits C++-bearing targets through a Ninja job pool without changing runtime code or ABI.

| CMake option | Default | Description |
| --- | --- | --- |
| `BROOKESIA_CXX_JOBS` | `6` | Maximum concurrent compile edges for selected C++ targets; `0` disables the pool |
| `BROOKESIA_FAST_COMPILE` | `OFF` | Adds `-g1` to selected C++ targets |
| `BROOKESIA_COMPILE_TUNING_INCLUDE_ESP_BOOST` | `ON` | Includes `esp-boost` in the pool when available |

The job pool is target-level, so C sources in a mixed C/C++ target use the same pool. GCC Brookesia targets also disable IPA clone passes to reduce per-translation-unit helper clones. The deprecated `BROOKESIA_SUPER_CXX_JOBS` and `BROOKESIA_SUPER_FAST_COMPILE` aliases remain accepted for existing System Super commands.

Projects with memory-intensive external ESP-IDF components can add those build component names explicitly:

```cmake
brookesia_compile_tuning_apply_current_project(
    EXTERNAL_COMPONENTS espressif__esp-dl
)
```
