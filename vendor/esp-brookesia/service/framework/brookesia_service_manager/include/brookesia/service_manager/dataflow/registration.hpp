/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file registration.hpp
 * @brief RAII provider registration handle for DataFlow adapters.
 */

#include <atomic>
#include <memory>
#include <string>

namespace esp_brookesia::service::dataflow {

class DataFlowRegistry;

/**
 * @brief RAII provider registration held by a provider service.
 */
class ProviderRegistration {
public:
    ProviderRegistration() = default;
    ProviderRegistration(const ProviderRegistration &) = delete;
    ProviderRegistration &operator=(const ProviderRegistration &) = delete;
    ProviderRegistration(ProviderRegistration &&other) noexcept;
    ProviderRegistration &operator=(ProviderRegistration &&other) noexcept;
    ~ProviderRegistration();

    bool is_valid() const;
    void release();

private:
    friend class DataFlowRegistry;
    ProviderRegistration(
        DataFlowRegistry *registry, std::string provider_id,
        std::weak_ptr<std::atomic<bool>> registry_alive
    );

    DataFlowRegistry *registry_ = nullptr;
    std::string provider_id_;
    std::weak_ptr<std::atomic<bool>> registry_alive_;
};

} // namespace esp_brookesia::service::dataflow
