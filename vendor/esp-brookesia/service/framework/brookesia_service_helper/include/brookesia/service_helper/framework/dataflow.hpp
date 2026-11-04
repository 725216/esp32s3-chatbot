/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "boost/json.hpp"

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_manager/dataflow/operation.hpp"
#include "brookesia/service_manager/helper/base.hpp"
#include "brookesia/service_manager/service/dataflow_service.hpp"

namespace esp_brookesia::service::helper {

/**
 * @brief Type-safe helper for the built-in DataFlow control-plane service.
 *
 * This helper intentionally exposes descriptions, operation identifiers, and
 * route controls only. Native frame, PCM, and mapped-buffer access belongs to
 * the strongly typed operations in @ref service::dataflow.
 */
class DataFlow: public Base<DataFlow> {
public:
    using Model = dataflow::Model;
    using ProviderInfo = dataflow::ProviderInfo;
    using OutputInfo = dataflow::OutputInfo;
    using OperationConfig = dataflow::OperationConfig;
    using OperationInfo = dataflow::OperationInfo;
    using FunctionId = DataFlowService::FunctionId;
    using EventId = DataFlowService::EventId;

    static constexpr std::string_view get_name()
    {
        return DataFlowService::get_name();
    }

    static std::span<const FunctionSchema> get_function_schemas()
    {
        return DataFlowService::get_static_function_schemas();
    }

    static std::span<const EventSchema> get_event_schemas()
    {
        return DataFlowService::get_static_event_schemas();
    }

    static std::expected<std::vector<ProviderInfo>, std::string> list_providers(uint32_t timeout_ms = 0)
    {
        return call_and_parse<std::vector<ProviderInfo>, boost::json::array>(FunctionId::ListProviders, timeout_ms);
    }

    static std::expected<std::vector<OutputInfo>, std::string> list_outputs(
        Model model, std::string provider_id = {}, uint32_t timeout_ms = 0
    )
    {
        return call_and_parse_with_args<std::vector<OutputInfo>, boost::json::array>(
                   FunctionId::ListOutputs, timeout_ms, BROOKESIA_DESCRIBE_TO_STR(model), std::move(provider_id)
               );
    }

    static std::expected<OperationInfo, std::string> create_operation(
        const OperationConfig &config, uint32_t timeout_ms = 0
    )
    {
        return call_and_parse_with_args<OperationInfo, boost::json::object>(
                   FunctionId::CreateOperation, timeout_ms, BROOKESIA_DESCRIBE_TO_JSON(config).as_object()
               );
    }

    static std::expected<void, std::string> destroy_operation(
        const std::string &operation_id, uint32_t timeout_ms = 0
    )
    {
        return call_void(FunctionId::DestroyOperation, timeout_ms, operation_id);
    }

    static std::expected<OperationInfo, std::string> get_operation(
        const std::string &operation_id, uint32_t timeout_ms = 0
    )
    {
        return call_and_parse_with_args<OperationInfo, boost::json::object>(
                   FunctionId::GetOperation, timeout_ms, operation_id
               );
    }

    static std::expected<std::vector<OperationInfo>, std::string> list_operations(uint32_t timeout_ms = 0)
    {
        return call_and_parse<std::vector<OperationInfo>, boost::json::array>(FunctionId::ListOperations, timeout_ms);
    }

    static std::expected<void, std::string> request_output(
        const std::string &operation_id, const std::string &output_name, uint32_t timeout_ms = 0
    )
    {
        return call_void(FunctionId::RequestOutput, timeout_ms, operation_id, output_name);
    }

    static std::expected<void, std::string> release_output(
        const std::string &operation_id, const std::string &output_name, uint32_t timeout_ms = 0
    )
    {
        return call_void(FunctionId::ReleaseOutput, timeout_ms, operation_id, output_name);
    }

    static std::expected<void, std::string> set_active_source(
        const std::string &operation_id, const std::string &output_name, std::string source_name = {},
        uint32_t timeout_ms = 0
    )
    {
        return call_void(
                   FunctionId::SetActiveSource, timeout_ms, operation_id, output_name, std::move(source_name)
               );
    }

    static std::expected<void, std::string> set_active_source_role(
        const std::string &operation_id, const std::string &output_name, const std::string &role,
        uint32_t timeout_ms = 0
    )
    {
        return call_void(FunctionId::SetActiveSourceRole, timeout_ms, operation_id, output_name, role);
    }

    static std::expected<std::string, std::string> get_active_source(
        const std::string &operation_id, const std::string &output_name, uint32_t timeout_ms = 0
    )
    {
        auto binding = ServiceManager::get_instance().bind(get_name().data());
        if (!binding.is_valid()) {
            return std::unexpected("Failed to bind DataFlow service");
        }
        return call_function_sync<std::string>(FunctionId::GetActiveSource, operation_id, output_name, Timeout(timeout_ms));
    }

private:
    template <typename Value, typename JsonValue>
    static std::expected<Value, std::string> call_and_parse(FunctionId function_id, uint32_t timeout_ms)
    {
        auto binding = ServiceManager::get_instance().bind(get_name().data());
        if (!binding.is_valid()) {
            return std::unexpected("Failed to bind DataFlow service");
        }

        auto result = call_function_sync<JsonValue>(function_id, Timeout(timeout_ms));
        if (!result) {
            return std::unexpected(result.error());
        }
        Value value;
        if (!BROOKESIA_DESCRIBE_FROM_JSON(*result, value)) {
            return std::unexpected("Failed to parse DataFlow service result");
        }
        return value;
    }

    template <typename Value, typename JsonValue, typename... Args>
    static std::expected<Value, std::string> call_and_parse_with_args(
        FunctionId function_id, uint32_t timeout_ms, Args &&... args
    )
    {
        auto binding = ServiceManager::get_instance().bind(get_name().data());
        if (!binding.is_valid()) {
            return std::unexpected("Failed to bind DataFlow service");
        }

        auto result = call_function_sync<JsonValue>(
                          function_id, std::forward<Args>(args)..., Timeout(timeout_ms)
                      );
        if (!result) {
            return std::unexpected(result.error());
        }
        Value value;
        if (!BROOKESIA_DESCRIBE_FROM_JSON(*result, value)) {
            return std::unexpected("Failed to parse DataFlow service result");
        }
        return value;
    }

    template <typename... Args>
    static std::expected<void, std::string> call_void(FunctionId function_id, uint32_t timeout_ms, Args &&... args)
    {
        auto binding = ServiceManager::get_instance().bind(get_name().data());
        if (!binding.is_valid()) {
            return std::unexpected("Failed to bind DataFlow service");
        }
        return call_function_sync<void>(function_id, std::forward<Args>(args)..., Timeout(timeout_ms));
    }
};

} // namespace esp_brookesia::service::helper
