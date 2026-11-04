/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/audio_impl.hpp"

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace esp_brookesia::hal {

#if BROOKESIA_HAL_LINUX_MEDIA_BACKEND_STUB

class AudioCodecPlayerLinuxStub: public audio::CodecPlayerIface {
public:
    explicit AudioCodecPlayerLinuxStub(std::shared_ptr<AudioOutputControl> output_control)
        : output_control_(ensure_audio_output_control(std::move(output_control)))
    {
    }

    bool open(const Config &config) override
    {
        std::lock_guard lock(mutex_);
        if ((config.bits == 0) || (config.channels == 0) || (config.sample_rate == 0)) {
            BROOKESIA_LOGE(
                "Invalid player config: bits(%1%), channels(%2%), sample_rate(%3%)",
                config.bits, config.channels, config.sample_rate
            );
            return false;
        }

        config_ = config;
        is_opened_ = true;
        total_bytes_written_ = 0;
        return true;
    }

    void close() override
    {
        std::lock_guard lock(mutex_);
        is_opened_ = false;
    }

    bool set_volume(uint8_t volume) override
    {
        output_control_->set_volume(volume);
        return true;
    }

    bool write_data(const uint8_t *data, size_t size) override
    {
        std::lock_guard lock(mutex_);
        if (data == nullptr) {
            BROOKESIA_LOGE("Player write data pointer is null");
            return false;
        }

        total_bytes_written_ += size;
        return true;
    }

    bool is_pa_on_off_supported() override
    {
        return true;
    }

    bool set_pa_on_off(bool on) override
    {
        return output_control_->set_pa_on_off(on);
    }

    bool is_pa_on() const override
    {
        return output_control_->is_pa_on();
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<AudioOutputControl> output_control_;
    Config config_{};
    bool is_opened_ = false;
    size_t total_bytes_written_ = 0;
};

class AudioCodecRecorderLinuxStub: public audio::CodecRecorderIface {
public:
    AudioCodecRecorderLinuxStub()
        : audio::CodecRecorderIface(make_recorder_info())
    {
    }

    bool open() override
    {
        std::lock_guard lock(mutex_);
        is_opened_ = true;
        return true;
    }

    void close() override
    {
        std::lock_guard lock(mutex_);
        is_opened_ = false;
    }

    bool read_data(uint8_t *data, size_t size) override
    {
        std::lock_guard lock(mutex_);
        if (data == nullptr) {
            BROOKESIA_LOGE("Recorder read data pointer is null");
            return false;
        }

        std::fill_n(data, size, static_cast<uint8_t>(0x5A));
        return true;
    }

    bool set_general_gain(float gain) override
    {
        std::lock_guard lock(mutex_);
        const_cast<Info &>(get_info()).general_gain = gain;
        return true;
    }

    bool set_channel_gains(const std::map<uint8_t, float> &gains) override
    {
        std::lock_guard lock(mutex_);
        const_cast<Info &>(get_info()).channel_gains = gains;
        return true;
    }

private:
    std::mutex mutex_;
    bool is_opened_ = false;
};

class AudioPlaybackLinuxStub: public audio::PlaybackIface {
public:
    bool open(EventCallback callback) override
    {
        {
            std::lock_guard lock(mutex_);
            callback_ = std::move(callback);
            state_ = audio::PlayState::Idle;
            is_opened_ = true;
        }
        notify(audio::PlayState::Idle);
        return true;
    }

    void close() override
    {
        std::lock_guard lock(mutex_);
        is_opened_ = false;
        state_ = audio::PlayState::Idle;
        current_url_.clear();
        callback_ = nullptr;
    }

    bool play(const std::string &url) override
    {
        {
            std::lock_guard lock(mutex_);
            if (!is_opened_ || url.empty()) {
                BROOKESIA_LOGE("Invalid play URL request: opened(%1%), url_empty(%2%)", is_opened_, url.empty());
                return false;
            }
            current_url_ = url;
            state_ = audio::PlayState::Playing;
        }
        notify(audio::PlayState::Playing);
        return true;
    }

    bool pause() override
    {
        {
            std::lock_guard lock(mutex_);
            if (!is_opened_ || (state_ != audio::PlayState::Playing)) {
                return false;
            }
            state_ = audio::PlayState::Paused;
        }
        notify(audio::PlayState::Paused);
        return true;
    }

    bool resume() override
    {
        {
            std::lock_guard lock(mutex_);
            if (!is_opened_ || (state_ != audio::PlayState::Paused)) {
                return false;
            }
            state_ = audio::PlayState::Playing;
        }
        notify(audio::PlayState::Playing);
        return true;
    }

    bool stop() override
    {
        {
            std::lock_guard lock(mutex_);
            if (!is_opened_) {
                return false;
            }
            state_ = audio::PlayState::Idle;
            current_url_.clear();
        }
        notify(audio::PlayState::Idle);
        return true;
    }

    bool is_opened() const override
    {
        std::lock_guard lock(mutex_);
        return is_opened_;
    }

private:
    void notify(audio::PlayState state)
    {
        EventCallback callback;
        {
            std::lock_guard lock(mutex_);
            callback = callback_;
        }
        if (callback) {
            callback(state);
        }
    }

    mutable std::mutex mutex_;
    EventCallback callback_;
    std::string current_url_;
    bool is_opened_ = false;
    audio::PlayState state_ = audio::PlayState::Idle;
};

class AudioEncoderLinuxStub: public audio::EncoderIface {
public:
    std::vector<std::string> get_afe_wake_words() override
    {
        return {};
    }

    bool start(const audio::EncoderDynamicConfig &config, Callbacks callbacks) override
    {
        if ((config.general.channels == 0) || (config.general.sample_bits == 0) || (config.general.sample_rate == 0)) {
            BROOKESIA_LOGE("Invalid encoder config");
            return false;
        }

        {
            std::lock_guard lock(mutex_);
            callbacks_ = std::move(callbacks);
            is_started_ = true;
            is_paused_ = false;
        }

        if (config.enable_afe) {
            notify_afe(audio::AfeEvent::WakeStart);
        }
        return true;
    }

    int read_encoded_data(uint8_t *data, size_t size) override
    {
        if ((data == nullptr) || (size == 0)) {
            return -1;
        }

        {
            std::lock_guard lock(mutex_);
            if (!is_started_) {
                return -1;
            }
            if (is_paused_) {
                return 0;
            }
        }

        notify_recorder_data();

        const std::array<uint8_t, 6> encoder_data = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
        if (size < encoder_data.size()) {
            return -1;
        }
        std::memcpy(data, encoder_data.data(), encoder_data.size());
        return static_cast<int>(encoder_data.size());
    }

    void stop() override
    {
        std::lock_guard lock(mutex_);
        is_started_ = false;
        is_paused_ = false;
        callbacks_ = {};
    }

    void pause() override
    {
        std::lock_guard lock(mutex_);
        if (is_started_) {
            is_paused_ = true;
        }
    }

    void resume() override
    {
        std::lock_guard lock(mutex_);
        if (is_started_) {
            is_paused_ = false;
        }
    }

    bool is_started() const override
    {
        std::lock_guard lock(mutex_);
        return is_started_;
    }

    bool is_paused() const override
    {
        std::lock_guard lock(mutex_);
        return is_paused_;
    }

private:
    void notify_afe(audio::AfeEvent event)
    {
        AfeEventCallback callback;
        {
            std::lock_guard lock(mutex_);
            callback = callbacks_.afe_event;
        }
        if (callback) {
            callback(event);
        }
    }

    void notify_recorder_data()
    {
        DataCallback recorder_callback;
        {
            std::lock_guard lock(mutex_);
            recorder_callback = callbacks_.recorder_data;
        }

        const std::array<uint8_t, 8> recorder_data = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
        if (recorder_callback) {
            recorder_callback(recorder_data.data(), recorder_data.size());
        }
    }

    mutable std::mutex mutex_;
    Callbacks callbacks_;
    bool is_started_ = false;
    bool is_paused_ = false;
};

class AudioDecoderLinuxStub: public audio::DecoderIface {
public:
    bool start(const audio::DecoderDynamicConfig &config) override
    {
        if ((config.general.channels == 0) || (config.general.sample_bits == 0) || (config.general.sample_rate == 0)) {
            BROOKESIA_LOGE("Invalid decoder config");
            return false;
        }

        std::lock_guard lock(mutex_);
        is_started_ = true;
        return true;
    }

    void stop() override
    {
        std::lock_guard lock(mutex_);
        is_started_ = false;
    }

    bool is_started() const override
    {
        std::lock_guard lock(mutex_);
        return is_started_;
    }

    bool feed_data(const uint8_t *data, size_t size) override
    {
        std::lock_guard lock(mutex_);
        return is_started_ && (data != nullptr) && (size > 0);
    }

private:
    mutable std::mutex mutex_;
    bool is_started_ = false;
};


namespace audio_detail {

std::shared_ptr<audio::CodecPlayerIface> make_stub_codec_player(
    std::shared_ptr<AudioOutputControl> output_control
)
{
    return std::make_shared<AudioCodecPlayerLinuxStub>(std::move(output_control));
}

std::shared_ptr<audio::CodecRecorderIface> make_stub_codec_recorder()
{
    return std::make_shared<AudioCodecRecorderLinuxStub>();
}

std::shared_ptr<audio::PlaybackIface> make_stub_playback()
{
    return std::make_shared<AudioPlaybackLinuxStub>();
}

std::shared_ptr<audio::EncoderIface> make_stub_encoder()
{
    return std::make_shared<AudioEncoderLinuxStub>();
}

std::shared_ptr<audio::DecoderIface> make_stub_decoder()
{
    return std::make_shared<AudioDecoderLinuxStub>();
}

} // namespace audio_detail

#endif // BROOKESIA_HAL_LINUX_MEDIA_BACKEND_STUB

} // namespace esp_brookesia::hal
