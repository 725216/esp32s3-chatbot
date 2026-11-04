/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <array>
#include <optional>
#include <utility>

#include "brookesia/service_manager/macro_configs.h"
#if !BROOKESIA_SERVICE_MANAGER_SERVICE_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/utils.hpp"
#include "brookesia/service_manager/common.hpp"
#include "brookesia/service_manager/dataflow/registry.hpp"
#include "brookesia/service_manager/service/dataflow_service.hpp"
#include "brookesia/service_manager/service/manager.hpp"

namespace esp_brookesia::service {
namespace {

constexpr std::string_view RUNTIME_APP_ID_CALL_CONTEXT_KEY = "brookesia.system.runtime_app_id";
using FunctionId = DataFlowService::FunctionId;
using EventId = DataFlowService::EventId;

FunctionParameterSchema make_parameter(std::string name, std::string description, FunctionValueType type,
                                       std::optional<FunctionValue> default_value = std::nullopt)
{
    return {
        .name = std::move(name),
        .description = std::move(description),
        .type = type,
        .default_value = std::move(default_value),
    };
}

FunctionSchema make_function_schema(
    FunctionId id, std::string description, std::optional<FunctionValueType> return_type = std::nullopt,
    std::vector<FunctionParameterSchema> parameters = {}
)
{
    FunctionSchema schema{
        .name = BROOKESIA_DESCRIBE_TO_STR(id),
        .description = std::move(description),
        .parameters = std::move(parameters),
    };
    if (return_type.has_value()) {
        schema.return_value = FunctionReturnSchema{
            .type = *return_type,
            .description = "Function result.",
        };
    }
    return schema;
}

EventItemSchema make_event_item(std::string name, std::string description, EventItemType type)
{
    return {
        .name = std::move(name),
        .description = std::move(description),
        .type = type,
    };
}

EventSchema make_event_schema(EventId id, std::string description, std::vector<EventItemSchema> items)
{
    return {
        .name = BROOKESIA_DESCRIBE_TO_STR(id),
        .description = std::move(description),
        .items = std::move(items),
    };
}

std::expected<dataflow::Model, std::string> parse_model(std::string_view value)
{
    dataflow::Model model{};
    if (!BROOKESIA_DESCRIBE_STR_TO_ENUM_FLEXIBLE(std::string(value), model)) {
        return std::unexpected("Unsupported data-flow model: " + std::string(value));
    }
    return model;
}

} // namespace

DataFlowService::DataFlowService(ServiceManager &manager)
    : ServiceBase({
    .name = get_name().data(),
    .description = "Control provider-neutral visual and audio data-flow operations.",
    .version = make_version(
        BROOKESIA_SERVICE_MANAGER_VER_MAJOR,
        BROOKESIA_SERVICE_MANAGER_VER_MINOR,
        BROOKESIA_SERVICE_MANAGER_VER_PATCH
    ),
    .bindable = true,
})
, manager_(manager)
{
}

std::span<const FunctionSchema> DataFlowService::get_static_function_schemas()
{
    static const std::array<FunctionSchema, BROOKESIA_DESCRIBE_ENUM_TO_NUM(FunctionId::Max)> SCHEMAS = {{
            make_function_schema(
                FunctionId::ListProviders, "List discovered data-flow providers.", FunctionValueType::Array
            ),
            make_function_schema(
                FunctionId::ListOutputs, "List provider outputs for one data-flow model.", FunctionValueType::Array,
            {
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionListOutputsParam::Model),
                    "Model: Visual, AudioPlayback, or AudioCapture.", FunctionValueType::String
                ),
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionListOutputsParam::ProviderId),
                "Optional provider id.", FunctionValueType::String, FunctionValue(std::string{})
                ),
            }
            ),
            make_function_schema(
                FunctionId::CreateOperation, "Create a manager-owned descriptive data-flow operation.",
                FunctionValueType::Object,
            {
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionCreateOperationParam::Config),
                    "Operation configuration. Frame and PCM payloads are not accepted.", FunctionValueType::Object
                ),
            }
            ),
            make_function_schema(
                FunctionId::DestroyOperation, "Destroy one operation owned by the calling runtime app.", std::nullopt,
            {
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionOperationIdParam::OperationId), "Operation id.",
                    FunctionValueType::String
                ),
            }
            ),
            make_function_schema(
                FunctionId::GetOperation, "Get one operation owned by the calling runtime app.", FunctionValueType::Object,
            {
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionOperationIdParam::OperationId), "Operation id.",
                    FunctionValueType::String
                ),
            }
            ),
            make_function_schema(
                FunctionId::ListOperations, "List operations owned by the calling runtime app.", FunctionValueType::Array
            ),
            make_function_schema(
                FunctionId::RequestOutput, "Request an output for an operation.", std::nullopt,
            {
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionOutputParam::OperationId), "Operation id.",
                    FunctionValueType::String
                ),
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionOutputParam::OutputName), "Output name.",
                    FunctionValueType::String
                ),
            }
            ),
            make_function_schema(
                FunctionId::ReleaseOutput, "Release an output for an operation.", std::nullopt,
            {
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionOutputParam::OperationId), "Operation id.",
                    FunctionValueType::String
                ),
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionOutputParam::OutputName), "Output name.",
                    FunctionValueType::String
                ),
            }
            ),
            make_function_schema(
                FunctionId::SetActiveSource, "Set an operation or provider-visible source active for an output.",
                std::nullopt,
            {
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceParam::OperationId), "Operation id.",
                    FunctionValueType::String
                ),
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceParam::OutputName), "Output name.",
                    FunctionValueType::String
                ),
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceParam::SourceName),
                    "Optional provider-visible source name; empty selects this operation source.",
                FunctionValueType::String, FunctionValue(std::string{})
                ),
            }
            ),
            make_function_schema(
                FunctionId::SetActiveSourceRole, "Set an active visual source role for an output.", std::nullopt,
            {
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceRoleParam::OperationId), "Operation id.",
                    FunctionValueType::String
                ),
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceRoleParam::OutputName), "Output name.",
                    FunctionValueType::String
                ),
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceRoleParam::Role), "Source role.",
                    FunctionValueType::String
                ),
            }
            ),
            make_function_schema(
                FunctionId::GetActiveSource, "Get the active source for an output.", FunctionValueType::String,
            {
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceParam::OperationId), "Operation id.",
                    FunctionValueType::String
                ),
                make_parameter(
                    BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceParam::OutputName), "Output name.",
                    FunctionValueType::String
                ),
            }
            ),
        }
    };
    return SCHEMAS;
}

std::span<const EventSchema> DataFlowService::get_static_event_schemas()
{
    static const std::array<EventSchema, BROOKESIA_DESCRIBE_ENUM_TO_NUM(EventId::Max)> SCHEMAS = {{
            make_event_schema(
                EventId::ProviderChanged, "A provider was registered or removed.",
            {
                make_event_item(
                    BROOKESIA_DESCRIBE_TO_STR(EventProviderChangedParam::Info), "Provider information.",
                    EventItemType::Object
                ),
                make_event_item(
                    BROOKESIA_DESCRIBE_TO_STR(EventProviderChangedParam::Registered), "Registration state.",
                    EventItemType::Boolean
                ),
            }
            ),
            make_event_schema(
                EventId::OutputChanged, "A provider output was registered or removed.",
            {
                make_event_item(
                    BROOKESIA_DESCRIBE_TO_STR(EventOutputChangedParam::Info), "Output information.",
                    EventItemType::Object
                ),
                make_event_item(
                    BROOKESIA_DESCRIBE_TO_STR(EventOutputChangedParam::Registered), "Registration state.",
                    EventItemType::Boolean
                ),
            }
            ),
            make_event_schema(
                EventId::OperationStateChanged, "A data-flow operation changed state.",
            {
                make_event_item(
                    BROOKESIA_DESCRIBE_TO_STR(EventOperationStateChangedParam::Info), "Operation information.",
                    EventItemType::Object
                ),
            }
            ),
            make_event_schema(
                EventId::ActiveSourceChanged, "An operation output changed active source.",
            {
                make_event_item(
                    BROOKESIA_DESCRIBE_TO_STR(EventActiveSourceChangedParam::Info), "Operation information.",
                    EventItemType::Object
                ),
                make_event_item(
                    BROOKESIA_DESCRIBE_TO_STR(EventActiveSourceChangedParam::OutputName), "Output name.",
                    EventItemType::String
                ),
                make_event_item(
                    BROOKESIA_DESCRIBE_TO_STR(EventActiveSourceChangedParam::SourceName), "Source name.",
                    EventItemType::String
                ),
            }
            ),
        }
    };
    return SCHEMAS;
}

bool DataFlowService::on_init()
{
    auto &registry = manager_.get_dataflow_registry();
    registry_connections_.emplace_back(registry.connect_provider_changed(
    [this](const dataflow::ProviderInfo & info, bool registered) {
        publish_provider_changed(info, registered);
    }
                                       ));
    registry_connections_.emplace_back(registry.connect_output_changed(
    [this](const dataflow::OutputInfo & info, bool registered) {
        publish_output_changed(info, registered);
    }
                                       ));
    registry_connections_.emplace_back(registry.connect_operation_state_changed(
    [this](const dataflow::OperationInfo & info) {
        publish_operation_state_changed(info);
    }
                                       ));
    registry_connections_.emplace_back(registry.connect_active_source_changed(
    [this](const dataflow::OperationInfo & info, const std::string & output_name, const std::string & source_name) {
        publish_active_source_changed(info, output_name, source_name);
    }
                                       ));
    return true;
}

void DataFlowService::on_deinit()
{
    registry_connections_.clear();
}

std::vector<FunctionSchema> DataFlowService::get_function_schemas()
{
    const auto schemas = get_static_function_schemas();
    return {schemas.begin(), schemas.end()};
}

std::vector<EventSchema> DataFlowService::get_event_schemas()
{
    const auto schemas = get_static_event_schemas();
    return {schemas.begin(), schemas.end()};
}

ServiceBase::FunctionHandlerMap DataFlowService::get_function_handlers()
{
    return {
        {
            BROOKESIA_DESCRIBE_TO_STR(FunctionId::ListProviders),
            [this](FunctionParameterMap &&)
            {
                const auto providers = manager_.get_dataflow_registry().list_providers();
                return to_function_result(std::expected<boost::json::array, std::string>(
                                              BROOKESIA_DESCRIBE_TO_JSON(providers).as_array()
                                          ));
            },
        },
        {
            BROOKESIA_DESCRIBE_TO_STR(FunctionId::ListOutputs),
            [this](FunctionParameterMap &&args)
            {
                const auto &model_name = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionListOutputsParam::Model)
                        ));
                const auto &provider_id = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionListOutputsParam::ProviderId)
                        ));
                auto model = parse_model(model_name);
                if (!model) {
                    return to_function_result(std::expected<boost::json::array, std::string>(
                                                  std::unexpected(model.error())
                                              ));
                }
                const auto outputs = manager_.get_dataflow_registry().list_outputs(*model, provider_id);
                return to_function_result(std::expected<boost::json::array, std::string>(
                                              BROOKESIA_DESCRIBE_TO_JSON(outputs).as_array()
                                          ));
            },
        },
        {
            BROOKESIA_DESCRIBE_TO_STR(FunctionId::CreateOperation),
            [this](FunctionParameterMap &&args)
            {
                const auto &config = std::get<boost::json::object>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionCreateOperationParam::Config)
                        ));
                return to_function_result(function_create_operation(config));
            },
        },
        {
            BROOKESIA_DESCRIBE_TO_STR(FunctionId::DestroyOperation),
            [this](FunctionParameterMap &&args)
            {
                const auto &operation_id = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionOperationIdParam::OperationId)
                        ));
                return to_function_result(function_destroy_operation(operation_id));
            },
        },
        {
            BROOKESIA_DESCRIBE_TO_STR(FunctionId::GetOperation),
            [this](FunctionParameterMap &&args)
            {
                const auto &operation_id = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionOperationIdParam::OperationId)
                        ));
                return to_function_result(function_get_operation(operation_id));
            },
        },
        {
            BROOKESIA_DESCRIBE_TO_STR(FunctionId::ListOperations),
            [this](FunctionParameterMap &&)
            {
                return to_function_result(function_list_operations());
            },
        },
        {
            BROOKESIA_DESCRIBE_TO_STR(FunctionId::RequestOutput),
            [this](FunctionParameterMap &&args)
            {
                const auto &operation_id = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionOutputParam::OperationId)
                        ));
                const auto &output_name = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionOutputParam::OutputName)
                        ));
                return to_function_result(function_request_output(operation_id, output_name));
            },
        },
        {
            BROOKESIA_DESCRIBE_TO_STR(FunctionId::ReleaseOutput),
            [this](FunctionParameterMap &&args)
            {
                const auto &operation_id = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionOutputParam::OperationId)
                        ));
                const auto &output_name = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionOutputParam::OutputName)
                        ));
                return to_function_result(function_release_output(operation_id, output_name));
            },
        },
        {
            BROOKESIA_DESCRIBE_TO_STR(FunctionId::SetActiveSource),
            [this](FunctionParameterMap &&args)
            {
                const auto &operation_id = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceParam::OperationId)
                        ));
                const auto &output_name = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceParam::OutputName)
                        ));
                const auto &source_name = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceParam::SourceName)
                        ));
                return to_function_result(function_set_active_source(operation_id, output_name, source_name));
            },
        },
        {
            BROOKESIA_DESCRIBE_TO_STR(FunctionId::SetActiveSourceRole),
            [this](FunctionParameterMap &&args)
            {
                const auto &operation_id = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceRoleParam::OperationId)
                        ));
                const auto &output_name = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceRoleParam::OutputName)
                        ));
                const auto &role = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceRoleParam::Role)
                        ));
                return to_function_result(function_set_active_source_role(operation_id, output_name, role));
            },
        },
        {
            BROOKESIA_DESCRIBE_TO_STR(FunctionId::GetActiveSource),
            [this](FunctionParameterMap &&args)
            {
                const auto &operation_id = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceParam::OperationId)
                        ));
                const auto &output_name = std::get<std::string>(args.at(
                            BROOKESIA_DESCRIBE_TO_STR(FunctionActiveSourceParam::OutputName)
                        ));
                return to_function_result(function_get_active_source(operation_id, output_name));
            },
        },
    };
}

std::expected<std::string, std::string> DataFlowService::get_owner() const
{
    auto owner = get_current_call_context_value(RUNTIME_APP_ID_CALL_CONTEXT_KEY);
    if (!owner.has_value() || owner->empty()) {
        return std::unexpected("DataFlow functions require a runtime app call context");
    }
    return *owner;
}

std::expected<std::shared_ptr<dataflow::DataFlowOperation>, std::string> DataFlowService::get_owned_operation(
    std::string_view operation_id
) const
{
    auto owner = get_owner();
    if (!owner) {
        return std::unexpected(owner.error());
    }
    auto operation = manager_.get_dataflow_registry().get_operation(operation_id);
    if (!operation) {
        return std::unexpected("DataFlow operation is not found: " + std::string(operation_id));
    }
    if (operation->get_owner() != *owner) {
        return std::unexpected("DataFlow operation is not owned by the calling runtime app");
    }
    return operation;
}

std::expected<boost::json::object, std::string> DataFlowService::function_create_operation(
    const boost::json::object &config_json
)
{
    dataflow::OperationConfig config;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(config_json, config)) {
        return std::unexpected("Failed to parse DataFlow operation configuration");
    }
    auto owner = get_owner();
    if (!owner) {
        return std::unexpected(owner.error());
    }
    config.owner = *owner;

    std::shared_ptr<dataflow::DataFlowOperation> operation;
    switch (config.model) {
    case dataflow::Model::Visual: {
        dataflow::VisualOperationConfig visual_config;
        static_cast<dataflow::OperationConfig &>(visual_config) = config;
        auto result = manager_.get_dataflow_registry().open_visual_operation(std::move(visual_config));
        if (!result) {
            return std::unexpected(result.error());
        }
        operation = std::move(*result);
        break;
    }
    case dataflow::Model::AudioPlayback: {
        dataflow::AudioPlaybackOperationConfig playback_config;
        static_cast<dataflow::OperationConfig &>(playback_config) = config;
        auto result = manager_.get_dataflow_registry().open_audio_playback_operation(std::move(playback_config));
        if (!result) {
            return std::unexpected(result.error());
        }
        operation = std::move(*result);
        break;
    }
    case dataflow::Model::AudioCapture: {
        dataflow::AudioCaptureOperationConfig capture_config;
        static_cast<dataflow::OperationConfig &>(capture_config) = config;
        auto result = manager_.get_dataflow_registry().open_audio_capture_operation(std::move(capture_config));
        if (!result) {
            return std::unexpected(result.error());
        }
        operation = std::move(*result);
        break;
    }
    default:
        return std::unexpected("Unsupported DataFlow operation model");
    }
    return BROOKESIA_DESCRIBE_TO_JSON(operation->get_info()).as_object();
}

std::expected<boost::json::object, std::string> DataFlowService::function_get_operation(
    std::string_view operation_id
) const
{
    auto operation = get_owned_operation(operation_id);
    if (!operation) {
        return std::unexpected(operation.error());
    }
    return BROOKESIA_DESCRIBE_TO_JSON((*operation)->get_info()).as_object();
}

std::expected<boost::json::array, std::string> DataFlowService::function_list_operations() const
{
    auto owner = get_owner();
    if (!owner) {
        return std::unexpected(owner.error());
    }
    std::vector<dataflow::OperationInfo> infos;
    for (const auto &operation : manager_.get_dataflow_registry().list_operations(*owner)) {
        infos.push_back(operation->get_info());
    }
    return BROOKESIA_DESCRIBE_TO_JSON(infos).as_array();
}

std::expected<void, std::string> DataFlowService::function_destroy_operation(std::string_view operation_id)
{
    auto operation = get_owned_operation(operation_id);
    if (!operation) {
        return std::unexpected(operation.error());
    }
    manager_.get_dataflow_registry().release_operation((*operation)->get_id());
    return {};
}

std::expected<void, std::string> DataFlowService::function_request_output(
    std::string_view operation_id, std::string_view output_name
)
{
    auto operation = get_owned_operation(operation_id);
    if (!operation) {
        return std::unexpected(operation.error());
    }
    if (auto visual = std::dynamic_pointer_cast<dataflow::VisualOperation>(*operation)) {
        return visual->request_output(output_name);
    }
    if (auto playback = std::dynamic_pointer_cast<dataflow::AudioPlaybackOperation>(*operation)) {
        return playback->request_output(output_name);
    }
    return std::unexpected("DataFlow operation does not support outputs");
}

std::expected<void, std::string> DataFlowService::function_release_output(
    std::string_view operation_id, std::string_view output_name
)
{
    auto operation = get_owned_operation(operation_id);
    if (!operation) {
        return std::unexpected(operation.error());
    }
    if (auto visual = std::dynamic_pointer_cast<dataflow::VisualOperation>(*operation)) {
        return visual->release_output(output_name);
    }
    if (auto playback = std::dynamic_pointer_cast<dataflow::AudioPlaybackOperation>(*operation)) {
        return playback->release_output(output_name);
    }
    return std::unexpected("DataFlow operation does not support outputs");
}

std::expected<void, std::string> DataFlowService::function_set_active_source(
    std::string_view operation_id, std::string_view output_name, std::string_view source_name
)
{
    auto operation = get_owned_operation(operation_id);
    if (!operation) {
        return std::unexpected(operation.error());
    }
    if (auto visual = std::dynamic_pointer_cast<dataflow::VisualOperation>(*operation)) {
        return visual->set_active_source_named(output_name, source_name);
    }
    if (auto playback = std::dynamic_pointer_cast<dataflow::AudioPlaybackOperation>(*operation)) {
        if (!source_name.empty()) {
            return std::unexpected("Audio playback operations only select their own source");
        }
        return playback->set_active_source(output_name);
    }
    return std::unexpected("DataFlow operation does not support active sources");
}

std::expected<void, std::string> DataFlowService::function_set_active_source_role(
    std::string_view operation_id, std::string_view output_name, std::string_view role
)
{
    auto operation = get_owned_operation(operation_id);
    if (!operation) {
        return std::unexpected(operation.error());
    }
    auto visual = std::dynamic_pointer_cast<dataflow::VisualOperation>(*operation);
    if (!visual) {
        return std::unexpected("Only visual DataFlow operations support source roles");
    }
    return visual->set_active_source_role(output_name, role);
}

std::expected<std::string, std::string> DataFlowService::function_get_active_source(
    std::string_view operation_id, std::string_view output_name
) const
{
    auto operation = get_owned_operation(operation_id);
    if (!operation) {
        return std::unexpected(operation.error());
    }
    if (auto visual = std::dynamic_pointer_cast<dataflow::VisualOperation>(*operation)) {
        return visual->get_active_source(output_name);
    }
    if (auto playback = std::dynamic_pointer_cast<dataflow::AudioPlaybackOperation>(*operation)) {
        return playback->get_active_source(output_name);
    }
    return std::unexpected("DataFlow operation does not support active sources");
}

void DataFlowService::publish_provider_changed(const dataflow::ProviderInfo &info, bool registered)
{
    (void)publish_event(
        BROOKESIA_DESCRIBE_TO_STR(EventId::ProviderChanged),
    EventItemMap{{
            BROOKESIA_DESCRIBE_TO_STR(EventProviderChangedParam::Info),
            BROOKESIA_DESCRIBE_TO_JSON(info).as_object(),
        }, {
            BROOKESIA_DESCRIBE_TO_STR(EventProviderChangedParam::Registered), registered,
        }},
    true
    );
}

void DataFlowService::publish_output_changed(const dataflow::OutputInfo &info, bool registered)
{
    (void)publish_event(
        BROOKESIA_DESCRIBE_TO_STR(EventId::OutputChanged),
    EventItemMap{{
            BROOKESIA_DESCRIBE_TO_STR(EventOutputChangedParam::Info),
            BROOKESIA_DESCRIBE_TO_JSON(info).as_object(),
        }, {
            BROOKESIA_DESCRIBE_TO_STR(EventOutputChangedParam::Registered), registered,
        }},
    true
    );
}

void DataFlowService::publish_operation_state_changed(const dataflow::OperationInfo &info)
{
    (void)publish_event(
        BROOKESIA_DESCRIBE_TO_STR(EventId::OperationStateChanged),
    EventItemMap{{
            BROOKESIA_DESCRIBE_TO_STR(EventOperationStateChangedParam::Info),
            BROOKESIA_DESCRIBE_TO_JSON(info).as_object(),
        }},
    true
    );
}

void DataFlowService::publish_active_source_changed(
    const dataflow::OperationInfo &info, const std::string &output_name, const std::string &source_name
)
{
    (void)publish_event(
        BROOKESIA_DESCRIBE_TO_STR(EventId::ActiveSourceChanged),
    EventItemMap{{
            BROOKESIA_DESCRIBE_TO_STR(EventActiveSourceChangedParam::Info),
            BROOKESIA_DESCRIBE_TO_JSON(info).as_object(),
        }, {
            BROOKESIA_DESCRIBE_TO_STR(EventActiveSourceChangedParam::OutputName), output_name,
        }, {
            BROOKESIA_DESCRIBE_TO_STR(EventActiveSourceChangedParam::SourceName), source_name,
        }},
    true
    );
}

} // namespace esp_brookesia::service
