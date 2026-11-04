# ESP-Brookesia Service Manager

* [中文版本](./README_CN.md)

## Overview

`brookesia_service_manager` is the service lifecycle, local-call, and event framework for ESP-Brookesia.

For more information, see the [ESP-Brookesia Programming Guide](https://docs.espressif.com/projects/esp-brookesia/en/latest/service/framework/manager/index.html).

## DataFlow Integration

The built-in `DataFlow` service discovers provider-owned visual, audio playback, and audio capture operations. Applications can list providers and outputs, create owner-scoped operations, and release them without depending on a concrete media provider.

Native frame, PCM, and mapped-buffer access remains available through the typed C++ operation interfaces; the service helper exposes the control-plane schemas and routing operations.

## How to Use

### Environment Requirements

Please refer to the following documentation:

- [ESP-Brookesia Programming Guide - Versioning](https://docs.espressif.com/projects/esp-brookesia/en/latest/getting_started.html#getting-started-versioning)
- [ESP-Brookesia Programming Guide - Development Environment Setup](https://docs.espressif.com/projects/esp-brookesia/en/latest/getting_started.html#getting-started-dev-environment)

### Add to Your Project

Please refer to [ESP-Brookesia Programming Guide - How to Obtain and Use Components](https://docs.espressif.com/projects/esp-brookesia/en/latest/getting_started.html#getting-started-component-usage).
