/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file dataflow.hpp
 * @brief Backward-compatible umbrella header for the DataFlow public API.
 *
 * New code should include the focused headers in `service_manager/dataflow/`
 * when it needs only one model or control layer. This umbrella preserves the
 * original public include path for existing applications and providers.
 */

#include "brookesia/service_manager/dataflow/model.hpp"
#include "brookesia/service_manager/dataflow/topology.hpp"
#include "brookesia/service_manager/dataflow/operation.hpp"
#include "brookesia/service_manager/dataflow/visual/types.hpp"
#include "brookesia/service_manager/dataflow/visual/operation.hpp"
#include "brookesia/service_manager/dataflow/audio/types.hpp"
#include "brookesia/service_manager/dataflow/audio/playback_operation.hpp"
#include "brookesia/service_manager/dataflow/audio/capture_operation.hpp"
#include "brookesia/service_manager/dataflow/provider.hpp"
#include "brookesia/service_manager/dataflow/registration.hpp"
#include "brookesia/service_manager/dataflow/registry.hpp"
