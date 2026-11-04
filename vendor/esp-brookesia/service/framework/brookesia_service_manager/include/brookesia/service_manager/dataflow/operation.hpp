/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file operation.hpp
 * @brief Provider-backed DataFlow operation lifetime and control snapshots.
 */

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "brookesia/service_manager/dataflow/topology.hpp"

namespace esp_brookesia::service::dataflow {

struct OperationConfig {
    std::string owner;
    std::string provider_id;
    Model model = Model::Visual;
    SourceInfo source;
    std::string output_name;
    bool request_output = false;
    bool activate_source = false;
};

/**
 * @brief Serializable snapshot of one manager-owned operation.
 *
 * This is deliberately control-plane-only. Frame, PCM, and mapped-buffer
 * payloads remain on the typed C++ operation interfaces.
 */
struct OperationInfo {
    std::string id;
    std::string owner;
    std::string provider_id;
    Model model = Model::Visual;
    SourceInfo source;
    std::string output_name;
    OperationState state = OperationState::Ready;
};

class DataFlowRegistry;

/**
 * @brief Base class for one provider-backed native data-flow route.
 *
 * The registry assigns an opaque string operation id and owns the provider
 * service binding for its lifetime. Calling close, output revocation, provider
 * de-registration, or app-owner cleanup makes the operation unavailable.
 */
class DataFlowOperation {
public:
    virtual ~DataFlowOperation();

    DataFlowOperation(const DataFlowOperation &) = delete;
    DataFlowOperation &operator=(const DataFlowOperation &) = delete;

    std::string get_id() const;
    std::string get_owner() const;
    std::string get_provider_id() const;
    Model get_model() const;
    OperationState get_state() const;
    OperationInfo get_info() const;
    bool is_available() const;
    void close();

protected:
    DataFlowOperation();
    virtual void on_close() = 0;
    void set_state(OperationState state);

private:
    friend class DataFlowRegistry;
    void set_registry_metadata(
        OperationInfo info,
        std::function<void(std::string_view)> release_callback,
        std::function<void(const OperationInfo &)> state_callback
    );

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace esp_brookesia::service::dataflow

namespace esp_brookesia::service::dataflow {

BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::OperationConfig,
    (),
    (owner, provider_id, model, source, output_name, request_output, activate_source)
);
BROOKESIA_DESCRIBE_STRUCT(
    esp_brookesia::service::dataflow::OperationInfo,
    (),
    (id, owner, provider_id, model, source, output_name, state)
);

} // namespace esp_brookesia::service::dataflow
