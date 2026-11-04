/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/audio_impl.hpp"

namespace esp_brookesia::service {

#if BROOKESIA_SERVICE_AUDIO_ENABLE_AUTO_REGISTER
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    ServiceBase, AudioPlayback, AudioPlayback::get_instance().get_attributes().name, AudioPlayback::get_instance(),
    BROOKESIA_SERVICE_AUDIO_PLAYBACK_PLUGIN_SYMBOL
);

BROOKESIA_PLUGIN_REGISTER_WITH_SYMBOL(
    ServiceBase, AudioEncoder, helper::AudioEncoder<0>::get_name().data(),
    BROOKESIA_SERVICE_AUDIO_ENCODER_PLUGIN_SYMBOL_0, 0
);

BROOKESIA_PLUGIN_REGISTER_WITH_SYMBOL(
    ServiceBase, AudioDecoder, helper::AudioDecoder<0>::get_name().data(),
    BROOKESIA_SERVICE_AUDIO_DECODER_PLUGIN_SYMBOL_0, 0
);
#endif // BROOKESIA_SERVICE_AUDIO_ENABLE_AUTO_REGISTER

} // namespace esp_brookesia::service
