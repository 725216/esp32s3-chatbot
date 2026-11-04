/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file types.hpp
 * @brief Visual output formats, frame descriptors, and mapped-buffer views.
 */

#include <cstddef>
#include <cstdint>
#include <span>

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_manager/dataflow/topology.hpp"

namespace esp_brookesia::service::dataflow {

enum class VisualPixelFormat : uint8_t {
    RGB565,
    RGB888,
    ARGB8888,
    Unknown,
};

enum class VisualOutputSlot : uint8_t {
    Device,
    Buffer,
};

enum class VisualByteOrder : uint8_t {
    Native,
    LittleEndian,
    BigEndian,
    Swap16,
};

struct VisualOutputInfo {
    OutputInfo output;
    VisualPixelFormat pixel_format = VisualPixelFormat::Unknown;
    VisualOutputSlot slot = VisualOutputSlot::Device;
    VisualByteOrder byte_order = VisualByteOrder::Native;
    size_t stride_bytes = 0;
};

struct VisualFrameInfo {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    VisualPixelFormat pixel_format = VisualPixelFormat::Unknown;
};

enum class VisualPresentResult : uint8_t {
    Presented,
    DroppedNotActive,
    DroppedInvalidFrame,
    DroppedQueueFull,
    Error,
};

enum class VisualPresentSubmitState : uint8_t {
    Queued,
    DroppedNotActive,
    DroppedInvalidFrame,
    Error,
};

struct VisualAsyncSubmitResult {
    uint32_t frame_id = 0;
    VisualPresentSubmitState state = VisualPresentSubmitState::Error;
};

struct VisualBufferView {
    VisualOutputInfo output;
    std::span<uint8_t> data;
    size_t stride_bytes = 0;
};

} // namespace esp_brookesia::service::dataflow

namespace esp_brookesia::service::dataflow {

BROOKESIA_DESCRIBE_ENUM(
    esp_brookesia::service::dataflow::VisualPixelFormat, RGB565, RGB888, ARGB8888, Unknown
);
BROOKESIA_DESCRIBE_ENUM(esp_brookesia::service::dataflow::VisualOutputSlot, Device, Buffer);
BROOKESIA_DESCRIBE_ENUM(
    esp_brookesia::service::dataflow::VisualByteOrder, Native, LittleEndian, BigEndian, Swap16
);
BROOKESIA_DESCRIBE_ENUM(
    esp_brookesia::service::dataflow::VisualPresentResult,
    Presented,
    DroppedNotActive,
    DroppedInvalidFrame,
    DroppedQueueFull,
    Error
);
BROOKESIA_DESCRIBE_ENUM(
    esp_brookesia::service::dataflow::VisualPresentSubmitState,
    Queued,
    DroppedNotActive,
    DroppedInvalidFrame,
    Error
);
BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::VisualOutputInfo, (), (output, pixel_format, slot, byte_order, stride_bytes)
);
BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::VisualFrameInfo, (), (x, y, width, height, pixel_format)
);
BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::VisualAsyncSubmitResult, (), (frame_id, state)
);

} // namespace esp_brookesia::service::dataflow
