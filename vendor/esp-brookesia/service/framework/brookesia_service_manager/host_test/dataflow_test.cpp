/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "brookesia/service_manager/dataflow/provider.hpp"
#include "brookesia/service_manager/dataflow/registry.hpp"
#include "brookesia/service_manager/dataflow/visual/operation.hpp"
#include "brookesia/service_manager/service/dataflow_service.hpp"
#include "brookesia/service_manager/service/manager.hpp"

using namespace esp_brookesia::service;

namespace {

bool require(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

class TestVisualOperation final : public dataflow::VisualOperation {
public:
    explicit TestVisualOperation(const std::shared_ptr<std::atomic<bool>> &closed)
        : closed_(closed)
    {
    }

    std::vector<dataflow::VisualOutputInfo> get_outputs() const override
    {
        return {{
                .output = {
                    .provider_id = "test.visual",
                    .name = "Output0",
                    .role = "display",
                    .id = 1,
                    .width = 64,
                    .height = 64,
                    .model = dataflow::Model::Visual,
                },
                .pixel_format = dataflow::VisualPixelFormat::RGB565,
                .slot = dataflow::VisualOutputSlot::Device,
                .byte_order = dataflow::VisualByteOrder::Native,
                .stride_bytes = 128,
            }};
    }

    std::vector<dataflow::SourceInfo> get_sources() const override
    {
        return {get_info().source};
    }

    std::vector<std::string> get_source_roles() const override
    {
        return {"display"};
    }

    std::expected<void, std::string> request_output(std::string_view output_name) override
    {
        if (output_name != "Output0") {
            return std::unexpected("Unknown output");
        }
        requested_ = true;
        return {};
    }

    std::expected<void, std::string> release_output(std::string_view output_name) override
    {
        if (output_name != "Output0") {
            return std::unexpected("Unknown output");
        }
        requested_ = false;
        return {};
    }

    std::expected<void, std::string> set_active_source(std::string_view output_name) override
    {
        return output_name == "Output0" ? std::expected<void, std::string> {} :
               std::unexpected("Unknown output");
    }

    std::expected<void, std::string> set_active_source_role(
        std::string_view output_name, std::string_view role
    ) override
    {
        return (output_name == "Output0") && (role == "display") ? std::expected<void, std::string> {} :
               std::unexpected("Unknown output or role");
    }

    std::expected<std::string, std::string> get_active_source(std::string_view output_name) const override
    {
        if (output_name != "Output0") {
            return std::unexpected("Unknown output");
        }
        return get_info().source.name;
    }

    std::expected<std::string, std::string> get_active_source_role(std::string_view output_name) const override
    {
        if (output_name != "Output0") {
            return std::unexpected("Unknown output");
        }
        return get_info().source.role;
    }

    dataflow::VisualPresentResult present_frame_sync(
        std::string_view output_name, const dataflow::VisualFrameInfo &, std::span<const uint8_t>, uint32_t
    ) override
    {
        return (output_name == "Output0") && requested_ ? dataflow::VisualPresentResult::Presented :
               dataflow::VisualPresentResult::Error;
    }

    dataflow::VisualPresentResult present_buffer_frame_sync(
        std::string_view output_name, const dataflow::VisualFrameInfo &, BufferWriter
    ) override
    {
        return (output_name == "Output0") && requested_ ? dataflow::VisualPresentResult::Presented :
               dataflow::VisualPresentResult::Error;
    }

    dataflow::VisualAsyncSubmitResult present_frame_async(
        std::string_view output_name, const dataflow::VisualFrameInfo &, std::span<const uint8_t>,
        CompletionCallback on_complete, uint32_t
    ) override
    {
        if (output_name != "Output0") {
            return {};
        }
        if (on_complete) {
            on_complete(1, dataflow::VisualPresentResult::Presented);
        }
        return {
            .frame_id = 1,
            .state = dataflow::VisualPresentSubmitState::Queued,
        };
    }

    std::expected<dataflow::VisualBufferView, std::string> map_output_buffer(
        std::string_view
    ) const override
    {
        return std::unexpected("Test provider has no mapped buffer");
    }

    esp_brookesia::lib_utils::connection connect_source_state_changed(SourceStateCallback) override
    {
        return {};
    }

    esp_brookesia::lib_utils::connection connect_active_source_changed(ActiveSourceCallback) override
    {
        return {};
    }

protected:
    void on_close() override
    {
        closed_->store(true);
    }

private:
    std::shared_ptr<std::atomic<bool>> closed_;
    bool requested_ = false;
};

class TestVisualProvider final : public dataflow::DataFlowProvider {
public:
    explicit TestVisualProvider(const std::shared_ptr<std::atomic<bool>> &closed)
        : closed_(closed)
    {
    }

    dataflow::ProviderInfo get_provider_info() const override
    {
        return {
            .id = "test.visual",
            .service_name = std::string(DataFlowService::get_name()),
            .description = "DataFlow host-test provider.",
            .models = {dataflow::Model::Visual},
        };
    }

    std::vector<dataflow::OutputInfo> list_outputs(dataflow::Model model) const override
    {
        if (model != dataflow::Model::Visual) {
            return {};
        }
        return {{
                .name = "Output0",
                .role = "display",
                .id = 1,
                .width = 64,
                .height = 64,
                .model = dataflow::Model::Visual,
            }};
    }

    std::expected<std::shared_ptr<dataflow::VisualOperation>, std::string> open_visual_operation(
        const dataflow::VisualOperationConfig &
    ) override
    {
        return std::make_shared<TestVisualOperation>(closed_);
    }

private:
    std::shared_ptr<std::atomic<bool>> closed_;
};

} // namespace

int main()
{
    auto &manager = ServiceManager::get_instance();
    if (!require(manager.init(), "Failed to initialize service manager") ||
            !require(manager.start(), "Failed to start service manager")) {
        manager.deinit();
        return EXIT_FAILURE;
    }

    auto closed = std::make_shared<std::atomic<bool>>(false);
    auto registration_result = manager.get_dataflow_registry().register_provider(
                                   std::make_shared<TestVisualProvider>(closed)
                               );
    if (!require(registration_result.has_value(), "Failed to register test DataFlow provider")) {
        manager.stop();
        manager.deinit();
        return EXIT_FAILURE;
    }
    auto registration = std::move(registration_result.value());

    const auto providers = manager.get_dataflow_registry().list_providers();
    if (!require(providers.size() == 1, "Unexpected DataFlow provider count") ||
            !require(providers.front().id == "test.visual", "Unexpected DataFlow provider id")) {
        registration.release();
        manager.stop();
        manager.deinit();
        return EXIT_FAILURE;
    }

    dataflow::VisualOperationConfig config;
    config.owner = "dataflow-host-test";
    config.source = {
        .name = "TestSource",
        .role = "display",
    };
    auto operation_result = manager.get_dataflow_registry().open_visual_operation(std::move(config));
    if (!require(operation_result.has_value(), "Failed to open test visual operation")) {
        registration.release();
        manager.stop();
        manager.deinit();
        return EXIT_FAILURE;
    }

    auto operation = std::move(operation_result.value());
    const auto outputs = operation->get_outputs();
    if (!require(operation->get_owner() == "dataflow-host-test", "Unexpected operation owner") ||
            !require(operation->get_provider_id() == "test.visual", "Unexpected operation provider") ||
            !require(outputs.size() == 1, "Unexpected operation output count") ||
            !require(operation->request_output("Output0").has_value(), "Failed to request operation output")) {
        operation->close();
        registration.release();
        manager.stop();
        manager.deinit();
        return EXIT_FAILURE;
    }

    operation->close();
    if (!require(closed->load(), "Operation close was not forwarded to the provider") ||
            !require(manager.get_dataflow_registry().list_operations("dataflow-host-test").empty(),
                     "Closed operation remained in the registry")) {
        registration.release();
        manager.stop();
        manager.deinit();
        return EXIT_FAILURE;
    }

    registration.release();
    if (!require(manager.get_dataflow_registry().list_providers().empty(),
                 "Released provider remained in the registry")) {
        manager.stop();
        manager.deinit();
        return EXIT_FAILURE;
    }

    manager.stop();
    manager.deinit();
    std::cout << "brookesia_service_manager DataFlow host test passed\n";
    return EXIT_SUCCESS;
}
