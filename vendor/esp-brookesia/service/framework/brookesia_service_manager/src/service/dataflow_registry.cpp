/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <atomic>
#include <exception>
#include <map>
#include <mutex>
#include <utility>

#include "private/utils.hpp"
#include "brookesia/service_manager/dataflow/registry.hpp"
#include "brookesia/service_manager/service/manager.hpp"

namespace esp_brookesia::service::dataflow {
namespace {

bool supports_model(const ProviderInfo &info, Model model)
{
    return std::find(info.models.begin(), info.models.end(), model) != info.models.end();
}

void disconnect_connections(std::vector<lib_utils::connection> &connections)
{
    for (const auto &connection : connections) {
        connection.disconnect();
    }
    connections.clear();
}

} // namespace

struct DataFlowRegistry::Impl {
    struct ProviderRecord {
        ProviderInfo info;
        std::shared_ptr<DataFlowProvider> provider;
        std::vector<OutputInfo> outputs;
        uint64_t generation = 0;
    };

    struct OperationRecord {
        std::shared_ptr<DataFlowOperation> operation;
        std::unique_ptr<ServiceBinding> binding;
        std::vector<lib_utils::connection> connections;
        uint64_t provider_generation = 0;
    };

    struct ProviderSelection {
        std::shared_ptr<DataFlowProvider> provider;
        ProviderInfo info;
        uint64_t generation = 0;
    };

    explicit Impl(ServiceManager &manager)
        : manager(manager)
        , alive(std::make_shared<std::atomic<bool>>(true))
    {}

    std::expected<ProviderSelection, std::string> select_provider(
        Model model, std::string_view provider_id
    ) const
    {
        std::lock_guard lock(mutex);
        std::optional<ProviderSelection> selected;
        for (const auto &[id, record] : providers) {
            if (!provider_id.empty() && (id != provider_id)) {
                continue;
            }
            if (!record.info.available || !supports_model(record.info, model)) {
                continue;
            }
            if (!selected.has_value() || (record.info.priority > selected->info.priority)) {
                selected = ProviderSelection{
                    .provider = record.provider,
                    .info = record.info,
                    .generation = record.generation,
                };
            }
        }
        if (!selected.has_value()) {
            const auto suffix = provider_id.empty() ? std::string{} : " '" + std::string(provider_id) + "'";
            return std::unexpected("No available data-flow provider for requested model" + suffix);
        }
        return *selected;
    }

    bool is_selection_current(const ProviderSelection &selection) const
    {
        std::lock_guard lock(mutex);
        return is_selection_current_locked(selection);
    }

    bool is_selection_current_locked(const ProviderSelection &selection) const
    {
        const auto provider_it = providers.find(selection.info.id);
        if ((provider_it == providers.end()) || (provider_it->second.generation != selection.generation)) {
            return false;
        }
        return provider_it->second.provider == selection.provider;
    }

    ServiceManager &manager;
    mutable std::mutex mutex;
    std::map<std::string, ProviderRecord> providers;
    std::map<std::string, OperationRecord> operations;
    uint64_t next_generation = 1;
    std::shared_ptr<std::atomic<bool>> alive;
    lib_utils::signal<void(const ProviderInfo &, bool)> provider_changed_signal;
    lib_utils::signal<void(const OutputInfo &, bool)> output_changed_signal;
    lib_utils::signal<void(const OperationInfo &)> operation_state_changed_signal;
    lib_utils::signal<void(const OperationInfo &, const std::string &, const std::string &)> active_source_changed_signal;
};

ProviderRegistration::ProviderRegistration(
    DataFlowRegistry *registry, std::string provider_id, std::weak_ptr<std::atomic<bool>> registry_alive
)
    : registry_(registry)
    , provider_id_(std::move(provider_id))
    , registry_alive_(std::move(registry_alive))
{
}

ProviderRegistration::ProviderRegistration(ProviderRegistration &&other) noexcept
    : registry_(other.registry_)
    , provider_id_(std::move(other.provider_id_))
    , registry_alive_(std::move(other.registry_alive_))
{
    other.registry_ = nullptr;
}

ProviderRegistration &ProviderRegistration::operator=(ProviderRegistration &&other) noexcept
{
    if (this != &other) {
        release();
        registry_ = other.registry_;
        provider_id_ = std::move(other.provider_id_);
        registry_alive_ = std::move(other.registry_alive_);
        other.registry_ = nullptr;
    }
    return *this;
}

ProviderRegistration::~ProviderRegistration()
{
    release();
}

bool ProviderRegistration::is_valid() const
{
    auto registry_alive = registry_alive_.lock();
    return registry_ && registry_alive && registry_alive->load();
}

void ProviderRegistration::release()
{
    auto registry_alive = registry_alive_.lock();
    if (registry_ && registry_alive && registry_alive->load()) {
        registry_->unregister_provider(provider_id_);
    }
    registry_ = nullptr;
    provider_id_.clear();
    registry_alive_.reset();
}

DataFlowRegistry::DataFlowRegistry(ServiceManager &manager)
    : impl_(std::make_unique<Impl>(manager))
{
}

DataFlowRegistry::~DataFlowRegistry()
{
    std::vector<Impl::OperationRecord> operation_records;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->alive->store(false);
        for (auto &[id, record] : impl_->operations) {
            (void)id;
            operation_records.push_back(std::move(record));
        }
        impl_->operations.clear();
        impl_->providers.clear();
    }

    for (auto &record : operation_records) {
        disconnect_connections(record.connections);
        auto info = record.operation->get_info();
        record.operation->set_registry_metadata(std::move(info), {}, {});
        record.operation->close();
    }
}

std::expected<ProviderRegistration, std::string> DataFlowRegistry::register_provider(
    std::shared_ptr<DataFlowProvider> provider
)
{
    if (!provider) {
        return std::unexpected("Cannot register a null data-flow provider");
    }

    ProviderInfo info;
    std::vector<OutputInfo> outputs;
    try {
        info = provider->get_provider_info();
        if (info.id.empty() || info.service_name.empty() || info.models.empty()) {
            return std::unexpected("Data-flow provider id, service name, and models must not be empty");
        }
        for (const auto model : info.models) {
            auto model_outputs = provider->list_outputs(model);
            for (auto &output : model_outputs) {
                output.provider_id = info.id;
                output.model = model;
                outputs.push_back(std::move(output));
            }
        }
    } catch (const std::exception &error) {
        return std::unexpected("Failed to inspect data-flow provider: " + std::string(error.what()));
    } catch (...) {
        return std::unexpected("Failed to inspect data-flow provider");
    }

    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->providers.contains(info.id)) {
            return std::unexpected("A data-flow provider with id '" + info.id + "' is already registered");
        }
        impl_->providers.emplace(
            info.id,
        Impl::ProviderRecord{
            .info = info,
            .provider = provider,
            .outputs = outputs,
            .generation = impl_->next_generation++,
        }
        );
    }

    impl_->provider_changed_signal(info, true);
    for (const auto &output : outputs) {
        impl_->output_changed_signal(output, true);
    }
    return ProviderRegistration(this, info.id, impl_->alive);
}

void DataFlowRegistry::unregister_provider(std::string_view provider_id)
{
    Impl::ProviderRecord provider_record;
    std::vector<Impl::OperationRecord> operation_records;
    {
        std::lock_guard lock(impl_->mutex);
        const auto provider_it = impl_->providers.find(std::string(provider_id));
        if (provider_it == impl_->providers.end()) {
            return;
        }
        provider_record = std::move(provider_it->second);
        impl_->providers.erase(provider_it);
        for (auto operation_it = impl_->operations.begin(); operation_it != impl_->operations.end();) {
            if (operation_it->second.operation->get_provider_id() == provider_id) {
                operation_records.push_back(std::move(operation_it->second));
                operation_it = impl_->operations.erase(operation_it);
            } else {
                ++operation_it;
            }
        }
    }

    for (auto &record : operation_records) {
        disconnect_connections(record.connections);
        record.operation->set_state(OperationState::Unavailable);
        record.operation->close();
    }
    for (const auto &output : provider_record.outputs) {
        impl_->output_changed_signal(output, false);
    }
    impl_->provider_changed_signal(provider_record.info, false);
}

void DataFlowRegistry::invalidate_provider_operations(std::string_view provider_id)
{
    std::vector<Impl::OperationRecord> operation_records;
    {
        std::lock_guard lock(impl_->mutex);
        const auto provider_it = impl_->providers.find(std::string(provider_id));
        if (provider_it == impl_->providers.end()) {
            return;
        }
        // A restart must not reuse a route created for the previous provider generation.
        provider_it->second.generation = impl_->next_generation++;
        for (auto operation_it = impl_->operations.begin(); operation_it != impl_->operations.end();) {
            if (operation_it->second.operation->get_provider_id() == provider_id) {
                operation_records.push_back(std::move(operation_it->second));
                operation_it = impl_->operations.erase(operation_it);
            } else {
                ++operation_it;
            }
        }
    }

    for (auto &record : operation_records) {
        disconnect_connections(record.connections);
        record.operation->set_state(OperationState::Unavailable);
        record.operation->close();
    }
}

std::vector<ProviderInfo> DataFlowRegistry::list_providers() const
{
    std::vector<ProviderInfo> providers;
    std::lock_guard lock(impl_->mutex);
    providers.reserve(impl_->providers.size());
    for (const auto &[id, record] : impl_->providers) {
        (void)id;
        auto info = record.info;
        info.available = info.available && static_cast<bool>(record.provider);
        providers.push_back(std::move(info));
    }
    return providers;
}

std::vector<OutputInfo> DataFlowRegistry::list_outputs(Model model, std::string_view provider_id) const
{
    std::vector<OutputInfo> outputs;
    std::lock_guard lock(impl_->mutex);
    for (const auto &[id, record] : impl_->providers) {
        if (!provider_id.empty() && (id != provider_id)) {
            continue;
        }
        if (!record.info.available || !record.provider) {
            continue;
        }
        for (const auto &output : record.outputs) {
            if (output.model == model) {
                outputs.push_back(output);
            }
        }
    }
    return outputs;
}

std::expected<std::shared_ptr<VisualOperation>, std::string> DataFlowRegistry::open_visual_operation(
    VisualOperationConfig config
)
{
    config.model = Model::Visual;
    auto selection = impl_->select_provider(Model::Visual, config.provider_id);
    if (!selection) {
        return std::unexpected(selection.error());
    }
    auto binding = impl_->manager.bind(selection->info.service_name);
    if (!binding.is_valid()) {
        return std::unexpected("Failed to bind data-flow provider service '" + selection->info.service_name + "'");
    }
    auto operation = selection->provider->open_visual_operation(config);
    if (!operation) {
        return std::unexpected(operation.error());
    }
    if (!*operation) {
        return std::unexpected("Data-flow provider returned a null visual operation");
    }
    if (!impl_->is_selection_current(*selection)) {
        (*operation)->close();
        return std::unexpected("Data-flow provider was unregistered while opening a visual operation");
    }

    OperationInfo info{
        .id = utils_generate_uuid(),
        .owner = config.owner,
        .provider_id = selection->info.id,
        .model = Model::Visual,
        .source = config.source,
        .output_name = config.output_name,
        .state = (*operation)->get_state(),
    };
    (*operation)->set_registry_metadata(
        info,
    [this](std::string_view operation_id) {
        remove_operation_record(operation_id);
    },
    [this](const OperationInfo & operation_info) {
        impl_->operation_state_changed_signal(operation_info);
    }
    );
    std::vector<lib_utils::connection> connections;
    const auto visual_operation = *operation;
    const std::weak_ptr<VisualOperation> weak_operation = visual_operation;
    connections.push_back(visual_operation->connect_active_source_changed(
    [this, weak_operation](const std::string & output_name, const std::string & source_name) {
        auto operation = weak_operation.lock();
        if (!operation) {
            return;
        }
        impl_->active_source_changed_signal(operation->get_info(), output_name, source_name);
    }
                          ));
    bool operation_added = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->is_selection_current_locked(*selection)) {
            operation_added = impl_->operations.emplace(
                                  info.id,
            Impl::OperationRecord{
                .operation = *operation,
                .binding = std::make_unique<ServiceBinding>(std::move(binding)),
                .connections = std::move(connections),
                .provider_generation = selection->generation,
            }
                              ).second;
        }
    }
    if (!operation_added) {
        disconnect_connections(connections);
        (*operation)->set_registry_metadata((*operation)->get_info(), {}, {});
        (*operation)->close();
        return std::unexpected("Data-flow provider was unregistered while opening a visual operation");
    }
    impl_->operation_state_changed_signal((*operation)->get_info());
    return *operation;
}

std::expected<std::shared_ptr<AudioPlaybackOperation>, std::string> DataFlowRegistry::open_audio_playback_operation(
    AudioPlaybackOperationConfig config
)
{
    config.model = Model::AudioPlayback;
    auto selection = impl_->select_provider(Model::AudioPlayback, config.provider_id);
    if (!selection) {
        return std::unexpected(selection.error());
    }
    auto binding = impl_->manager.bind(selection->info.service_name);
    if (!binding.is_valid()) {
        return std::unexpected("Failed to bind data-flow provider service '" + selection->info.service_name + "'");
    }
    auto operation = selection->provider->open_audio_playback_operation(config);
    if (!operation) {
        return std::unexpected(operation.error());
    }
    if (!*operation) {
        return std::unexpected("Data-flow provider returned a null audio playback operation");
    }
    if (!impl_->is_selection_current(*selection)) {
        (*operation)->close();
        return std::unexpected("Data-flow provider was unregistered while opening an audio playback operation");
    }

    OperationInfo info{
        .id = utils_generate_uuid(),
        .owner = config.owner,
        .provider_id = selection->info.id,
        .model = Model::AudioPlayback,
        .source = config.source,
        .output_name = config.output_name,
        .state = (*operation)->get_state(),
    };
    (*operation)->set_registry_metadata(
        info,
    [this](std::string_view operation_id) {
        remove_operation_record(operation_id);
    },
    [this](const OperationInfo & operation_info) {
        impl_->operation_state_changed_signal(operation_info);
    }
    );
    bool operation_added = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->is_selection_current_locked(*selection)) {
            operation_added = impl_->operations.emplace(
                                  info.id,
            Impl::OperationRecord{
                .operation = *operation,
                .binding = std::make_unique<ServiceBinding>(std::move(binding)),
                .connections = {},
                .provider_generation = selection->generation,
            }
                              ).second;
        }
    }
    if (!operation_added) {
        (*operation)->set_registry_metadata((*operation)->get_info(), {}, {});
        (*operation)->close();
        return std::unexpected("Data-flow provider was unregistered while opening an audio playback operation");
    }
    impl_->operation_state_changed_signal((*operation)->get_info());
    return *operation;
}

std::expected<std::shared_ptr<AudioCaptureOperation>, std::string> DataFlowRegistry::open_audio_capture_operation(
    AudioCaptureOperationConfig config
)
{
    config.model = Model::AudioCapture;
    auto selection = impl_->select_provider(Model::AudioCapture, config.provider_id);
    if (!selection) {
        return std::unexpected(selection.error());
    }
    auto binding = impl_->manager.bind(selection->info.service_name);
    if (!binding.is_valid()) {
        return std::unexpected("Failed to bind data-flow provider service '" + selection->info.service_name + "'");
    }
    auto operation = selection->provider->open_audio_capture_operation(config);
    if (!operation) {
        return std::unexpected(operation.error());
    }
    if (!*operation) {
        return std::unexpected("Data-flow provider returned a null audio capture operation");
    }
    if (!impl_->is_selection_current(*selection)) {
        (*operation)->close();
        return std::unexpected("Data-flow provider was unregistered while opening an audio capture operation");
    }

    OperationInfo info{
        .id = utils_generate_uuid(),
        .owner = config.owner,
        .provider_id = selection->info.id,
        .model = Model::AudioCapture,
        .source = config.source,
        .output_name = config.output_name,
        .state = (*operation)->get_state(),
    };
    (*operation)->set_registry_metadata(
        info,
    [this](std::string_view operation_id) {
        remove_operation_record(operation_id);
    },
    [this](const OperationInfo & operation_info) {
        impl_->operation_state_changed_signal(operation_info);
    }
    );
    bool operation_added = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->is_selection_current_locked(*selection)) {
            operation_added = impl_->operations.emplace(
                                  info.id,
            Impl::OperationRecord{
                .operation = *operation,
                .binding = std::make_unique<ServiceBinding>(std::move(binding)),
                .connections = {},
                .provider_generation = selection->generation,
            }
                              ).second;
        }
    }
    if (!operation_added) {
        (*operation)->set_registry_metadata((*operation)->get_info(), {}, {});
        (*operation)->close();
        return std::unexpected("Data-flow provider was unregistered while opening an audio capture operation");
    }
    impl_->operation_state_changed_signal((*operation)->get_info());
    return *operation;
}

std::shared_ptr<DataFlowOperation> DataFlowRegistry::get_operation(std::string_view operation_id) const
{
    std::lock_guard lock(impl_->mutex);
    const auto operation_it = impl_->operations.find(std::string(operation_id));
    return operation_it == impl_->operations.end() ? nullptr : operation_it->second.operation;
}

std::vector<std::shared_ptr<DataFlowOperation>> DataFlowRegistry::list_operations(std::string_view owner) const
{
    std::vector<std::shared_ptr<DataFlowOperation>> operations;
    std::lock_guard lock(impl_->mutex);
    operations.reserve(impl_->operations.size());
    for (const auto &[id, record] : impl_->operations) {
        (void)id;
        if (!owner.empty() && (record.operation->get_owner() != owner)) {
            continue;
        }
        operations.push_back(record.operation);
    }
    return operations;
}

void DataFlowRegistry::release_operation(std::string_view operation_id)
{
    auto operation = get_operation(operation_id);
    if (operation) {
        operation->close();
    }
}

void DataFlowRegistry::remove_operation_record(std::string_view operation_id)
{
    Impl::OperationRecord record;
    {
        std::lock_guard lock(impl_->mutex);
        const auto operation_it = impl_->operations.find(std::string(operation_id));
        if (operation_it == impl_->operations.end()) {
            return;
        }
        record = std::move(operation_it->second);
        impl_->operations.erase(operation_it);
    }

    disconnect_connections(record.connections);
    record.binding.reset();
}

void DataFlowRegistry::release_operations_for_owner(std::string_view owner)
{
    auto operations = list_operations(owner);
    for (const auto &operation : operations) {
        operation->close();
    }
}

lib_utils::connection DataFlowRegistry::connect_provider_changed(ProviderChangedCallback callback)
{
    return impl_->provider_changed_signal.connect(std::move(callback));
}

lib_utils::connection DataFlowRegistry::connect_output_changed(OutputChangedCallback callback)
{
    return impl_->output_changed_signal.connect(std::move(callback));
}

lib_utils::connection DataFlowRegistry::connect_operation_state_changed(OperationStateChangedCallback callback)
{
    return impl_->operation_state_changed_signal.connect(std::move(callback));
}

lib_utils::connection DataFlowRegistry::connect_active_source_changed(ActiveSourceChangedCallback callback)
{
    return impl_->active_source_changed_signal.connect(std::move(callback));
}

} // namespace esp_brookesia::service::dataflow
