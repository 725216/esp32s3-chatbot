/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "brookesia/lib_utils/signal.hpp"
#include "brookesia/service_manager/dataflow/operation.hpp"
#include "brookesia/service_manager/service/base.hpp"

namespace esp_brookesia::service {

class ServiceManager;

/**
 * @brief Built-in control-plane service for manager-owned data-flow operations.
 *
 * It intentionally exposes only descriptive topology and operation controls.
 * Native frame, PCM, and borrowed-buffer operations stay on the typed C++ API
 * in @ref dataflow.hpp.
 */
class DataFlowService final : public ServiceBase {
public:
    enum class FunctionId : uint8_t {
        ListProviders,
        ListOutputs,
        CreateOperation,
        DestroyOperation,
        GetOperation,
        ListOperations,
        RequestOutput,
        ReleaseOutput,
        SetActiveSource,
        SetActiveSourceRole,
        GetActiveSource,
        Max,
    };

    enum class EventId : uint8_t {
        ProviderChanged,
        OutputChanged,
        OperationStateChanged,
        ActiveSourceChanged,
        Max,
    };

    enum class FunctionListOutputsParam : uint8_t { Model, ProviderId };
    enum class FunctionCreateOperationParam : uint8_t { Config };
    enum class FunctionOperationIdParam : uint8_t { OperationId };
    enum class FunctionOutputParam : uint8_t { OperationId, OutputName };
    enum class FunctionActiveSourceParam : uint8_t { OperationId, OutputName, SourceName };
    enum class FunctionActiveSourceRoleParam : uint8_t { OperationId, OutputName, Role };
    enum class EventProviderChangedParam : uint8_t { Info, Registered };
    enum class EventOutputChangedParam : uint8_t { Info, Registered };
    enum class EventOperationStateChangedParam : uint8_t { Info };
    enum class EventActiveSourceChangedParam : uint8_t { Info, OutputName, SourceName };

    explicit DataFlowService(ServiceManager &manager);

    static constexpr std::string_view get_name()
    {
        return "DataFlow";
    }

    static std::span<const FunctionSchema> get_static_function_schemas();
    static std::span<const EventSchema> get_static_event_schemas();

private:
    bool on_init() override;
    void on_deinit() override;
    std::vector<FunctionSchema> get_function_schemas() override;
    std::vector<EventSchema> get_event_schemas() override;
    FunctionHandlerMap get_function_handlers() override;

    std::expected<std::string, std::string> get_owner() const;
    std::expected<std::shared_ptr<dataflow::DataFlowOperation>, std::string> get_owned_operation(
        std::string_view operation_id
    ) const;
    std::expected<boost::json::object, std::string> function_create_operation(const boost::json::object &config_json);
    std::expected<boost::json::object, std::string> function_get_operation(std::string_view operation_id) const;
    std::expected<boost::json::array, std::string> function_list_operations() const;
    std::expected<void, std::string> function_destroy_operation(std::string_view operation_id);
    std::expected<void, std::string> function_request_output(
        std::string_view operation_id, std::string_view output_name
    );
    std::expected<void, std::string> function_release_output(
        std::string_view operation_id, std::string_view output_name
    );
    std::expected<void, std::string> function_set_active_source(
        std::string_view operation_id, std::string_view output_name, std::string_view source_name
    );
    std::expected<void, std::string> function_set_active_source_role(
        std::string_view operation_id, std::string_view output_name, std::string_view role
    );
    std::expected<std::string, std::string> function_get_active_source(
        std::string_view operation_id, std::string_view output_name
    ) const;
    void publish_provider_changed(const dataflow::ProviderInfo &info, bool registered);
    void publish_output_changed(const dataflow::OutputInfo &info, bool registered);
    void publish_operation_state_changed(const dataflow::OperationInfo &info);
    void publish_active_source_changed(
        const dataflow::OperationInfo &info, const std::string &output_name, const std::string &source_name
    );

    ServiceManager &manager_;
    std::vector<lib_utils::scoped_connection> registry_connections_;
};

BROOKESIA_DESCRIBE_ENUM(
    DataFlowService::FunctionId,
    ListProviders,
    ListOutputs,
    CreateOperation,
    DestroyOperation,
    GetOperation,
    ListOperations,
    RequestOutput,
    ReleaseOutput,
    SetActiveSource,
    SetActiveSourceRole,
    GetActiveSource,
    Max
);
BROOKESIA_DESCRIBE_ENUM(
    DataFlowService::EventId,
    ProviderChanged,
    OutputChanged,
    OperationStateChanged,
    ActiveSourceChanged,
    Max
);
BROOKESIA_DESCRIBE_ENUM(DataFlowService::FunctionListOutputsParam, Model, ProviderId);
BROOKESIA_DESCRIBE_ENUM(DataFlowService::FunctionCreateOperationParam, Config);
BROOKESIA_DESCRIBE_ENUM(DataFlowService::FunctionOperationIdParam, OperationId);
BROOKESIA_DESCRIBE_ENUM(DataFlowService::FunctionOutputParam, OperationId, OutputName);
BROOKESIA_DESCRIBE_ENUM(DataFlowService::FunctionActiveSourceParam, OperationId, OutputName, SourceName);
BROOKESIA_DESCRIBE_ENUM(DataFlowService::FunctionActiveSourceRoleParam, OperationId, OutputName, Role);
BROOKESIA_DESCRIBE_ENUM(DataFlowService::EventProviderChangedParam, Info, Registered);
BROOKESIA_DESCRIBE_ENUM(DataFlowService::EventOutputChangedParam, Info, Registered);
BROOKESIA_DESCRIBE_ENUM(DataFlowService::EventOperationStateChangedParam, Info);
BROOKESIA_DESCRIBE_ENUM(DataFlowService::EventActiveSourceChangedParam, Info, OutputName, SourceName);

} // namespace esp_brookesia::service
