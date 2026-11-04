#pragma once

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cstdint>
#include <memory>
#include <map>
#include <mutex>
#include <utility>

#include "brookesia/hal_linux/macro_configs.h"
#if !BROOKESIA_HAL_LINUX_AUDIO_DEVICE_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/utils.hpp"
#include "brookesia/hal_interface/interfaces/audio/codec_player.hpp"
#include "brookesia/hal_interface/interfaces/audio/codec_recorder.hpp"
#include "brookesia/hal_interface/interfaces/audio/processor.hpp"

namespace esp_brookesia::hal::audio_detail {

class AudioOutputControl {
public:
    void set_volume(uint8_t volume)
    {
        std::lock_guard lock(mutex_);
        volume_ = std::min<uint8_t>(volume, 100);
    }

    bool set_pa_on_off(bool on)
    {
        std::lock_guard lock(mutex_);
        pa_on_ = on;
        return true;
    }

    bool is_pa_on() const
    {
        std::lock_guard lock(mutex_);
        return pa_on_;
    }

    float get_gain() const
    {
        std::lock_guard lock(mutex_);
        return pa_on_ ? static_cast<float>(volume_) / 100.0f : 0.0f;
    }

private:
    mutable std::mutex mutex_;
    uint8_t volume_ = 100;
    bool pa_on_ = true;
};

inline std::shared_ptr<AudioOutputControl> ensure_audio_output_control(
    std::shared_ptr<AudioOutputControl> output_control
)
{
    return output_control != nullptr ? std::move(output_control) : std::make_shared<AudioOutputControl>();
}

inline audio::CodecRecorderIface::Info make_recorder_info()
{
    return audio::CodecRecorderIface::Info{
        .bits = 16,
        .channels = 2,
        .sample_rate = 16000,
        .mic_layout = "LR",
        .general_gain = 1.0f,
        .channel_gains = {
            {0, 1.0f},
            {1, 1.0f},
        },
    };
}

std::shared_ptr<audio::CodecPlayerIface> make_stub_codec_player(std::shared_ptr<AudioOutputControl> output_control);
std::shared_ptr<audio::CodecRecorderIface> make_stub_codec_recorder();
std::shared_ptr<audio::PlaybackIface> make_stub_playback();
std::shared_ptr<audio::EncoderIface> make_stub_encoder();
std::shared_ptr<audio::DecoderIface> make_stub_decoder();

} // namespace esp_brookesia::hal::audio_detail

namespace esp_brookesia::hal {
using audio_detail::AudioOutputControl;
using audio_detail::ensure_audio_output_control;
using audio_detail::make_recorder_info;
} // namespace esp_brookesia::hal
