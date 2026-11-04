/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file model.hpp
 * @brief DataFlow model and lifecycle state enums.
 */

#include <cstdint>

#include "brookesia/lib_utils/describe_helpers.hpp"

namespace esp_brookesia::service::dataflow {

enum class Model : uint8_t {
    Visual,
    AudioPlayback,
    AudioCapture,
};

enum class OperationState : uint8_t {
    Ready,
    Active,
    Unavailable,
    Closed,
    Error,
};

enum class SourceState : uint8_t {
    Registered,
    Requested,
    Granted,
    Released,
    Revoked,
};

} // namespace esp_brookesia::service::dataflow

// Boost.Describe enum descriptors use ADL, so these declarations must live in
// the namespace associated with the DataFlow public types.
namespace esp_brookesia::service::dataflow {

BROOKESIA_DESCRIBE_ENUM(esp_brookesia::service::dataflow::Model, Visual, AudioPlayback, AudioCapture);
BROOKESIA_DESCRIBE_ENUM(
    esp_brookesia::service::dataflow::OperationState, Ready, Active, Unavailable, Closed, Error
);
BROOKESIA_DESCRIBE_ENUM(
    esp_brookesia::service::dataflow::SourceState, Registered, Requested, Granted, Released, Revoked
);

} // namespace esp_brookesia::service::dataflow
