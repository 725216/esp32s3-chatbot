/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file topology.hpp
 * @brief Serializable DataFlow provider, source, and output descriptors.
 */

#include <cstdint>
#include <string>
#include <vector>

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_manager/dataflow/model.hpp"

namespace esp_brookesia::service::dataflow {

/**
 * @brief Provider identity and advertised operation models.
 *
 * A provider is registered by its owning service during `on_init()`. The
 * registry binds that service only after a caller opens an operation.
 */
struct ProviderInfo {
    std::string id;
    std::string service_name;
    std::string description;
    std::vector<Model> models;
    int priority = 0;
    bool available = true;
};

struct SourceInfo {
    std::string name;
    std::string role;
    std::vector<std::string> preferred_outputs;
    int priority = 0;
};

/**
 * @brief Shared output descriptor used by control-plane functions.
 *
 * Fields irrelevant to a model remain at their defaults. This keeps JS/Lua
 * topology APIs descriptive and prevents frame/PCM buffers from crossing the
 * service-function boundary.
 */
struct OutputInfo {
    std::string provider_id;
    std::string name;
    std::string role;
    uint32_t id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    uint8_t sample_bits = 0;
    Model model = Model::Visual;
};

} // namespace esp_brookesia::service::dataflow

namespace esp_brookesia::service::dataflow {

BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::ProviderInfo, (), (id, service_name, description, models, priority, available)
);
BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::SourceInfo, (), (name, role, preferred_outputs, priority)
);
BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::OutputInfo,
    (),
    (provider_id, name, role, id, width, height, sample_rate, channels, sample_bits, model)
);

} // namespace esp_brookesia::service::dataflow
