/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <expected>
#include <string>

#include "brookesia/service_manager/dataflow/registration.hpp"

namespace esp_brookesia::service {

class AudioDecoder;
class AudioEncoder;

std::expected<dataflow::ProviderRegistration, std::string> register_audio_decoder_dataflow_provider(
    AudioDecoder &decoder, int id
);

std::expected<dataflow::ProviderRegistration, std::string> register_audio_encoder_dataflow_provider(
    AudioEncoder &encoder, int id
);

} // namespace esp_brookesia::service
