/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <atomic>
#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "brookesia/service_audio/macro_configs.h"
#if !BROOKESIA_SERVICE_AUDIO_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "brookesia/service_audio/service_audio.hpp"
#include "brookesia/service_manager/dataflow/audio/capture_operation.hpp"
#include "brookesia/service_manager/dataflow/audio/playback_operation.hpp"
#include "brookesia/service_manager/dataflow/provider.hpp"
#include "brookesia/service_manager/dataflow/registry.hpp"
#include "brookesia/service_manager/service/manager.hpp"
#include "private/dataflow_provider.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::service {

namespace {

namespace df = dataflow;

constexpr const char *DECODER_OUTPUT_NAME = "Speaker0";
constexpr const char *DECODER_OUTPUT_ROLE = "speaker";
constexpr size_t PCM_FETCH_DATA_SIZE_MARGIN = 100;

std::string make_provider_id(std::string_view model, int id)
{
    return "audio." + std::string(model) + "." + std::to_string(id);
}

std::expected<AudioCodecFormat, std::string> to_audio_codec_format(df::AudioCodecFormat format)
{
    switch (format) {
    case df::AudioCodecFormat::PCM:
        return AudioCodecFormat::PCM;
    case df::AudioCodecFormat::OPUS:
        return AudioCodecFormat::OPUS;
    case df::AudioCodecFormat::G711A:
        return AudioCodecFormat::G711A;
    case df::AudioCodecFormat::Unknown:
    default:
        return std::unexpected("Unsupported audio codec format");
    }
}

df::AudioWriteResult to_dataflow_audio_write_result(AudioWriteResult result)
{
    switch (result) {
    case AudioWriteResult::Written:
        return df::AudioWriteResult::Written;
    case AudioWriteResult::DroppedNotActive:
        return df::AudioWriteResult::DroppedNotActive;
    case AudioWriteResult::DroppedQueueFull:
        return df::AudioWriteResult::DroppedQueueFull;
    case AudioWriteResult::DroppedInvalidData:
        return df::AudioWriteResult::DroppedInvalidData;
    case AudioWriteResult::Error:
    default:
        return df::AudioWriteResult::Error;
    }
}

df::AudioAfeEvent to_dataflow_audio_afe_event(AudioAFE_Event event)
{
    switch (event) {
    case AudioAFE_Event::VAD_Start:
        return df::AudioAfeEvent::VadStart;
    case AudioAFE_Event::VAD_End:
        return df::AudioAfeEvent::VadEnd;
    case AudioAFE_Event::WakeStart:
        return df::AudioAfeEvent::WakeStart;
    case AudioAFE_Event::WakeEnd:
        return df::AudioAfeEvent::WakeEnd;
    case AudioAFE_Event::Max:
    default:
        return df::AudioAfeEvent::Unknown;
    }
}

df::OutputInfo to_dataflow_output_info(const AudioOutputInfo &info, const std::string &provider_id)
{
    return {
        .provider_id = provider_id,
        .name = info.name,
        .role = info.role,
        .id = info.id,
        .sample_rate = info.sample_rates.empty() ? 0 : info.sample_rates.front(),
.channels = info.channels.empty() ? uint8_t{0} : info.channels.front(),
.sample_bits = info.sample_bits.empty() ? uint8_t{0} : info.sample_bits.front(),
        .model = df::Model::AudioPlayback,
    };
}

AudioSourceInfo to_audio_source_info(const df::SourceInfo &info)
{
    return {
        .name = info.name,
        .role = info.role,
        .preferred_outputs = info.preferred_outputs,
        .priority = info.priority,
    };
}

std::expected<AudioStreamConfig, std::string> to_audio_stream_config(const df::AudioStreamConfig &config)
{
    auto type = to_audio_codec_format(config.type);
    if (!type) {
        return std::unexpected(type.error());
    }

    return AudioStreamConfig{
        .type = type.value(),
        .general = AudioCodecGeneralConfig{
            .channels = config.general.channels,
            .sample_bits = config.general.sample_bits,
            .sample_rate = config.general.sample_rate,
            .frame_duration = config.general.frame_duration,
        },
        .queue_size_bytes = config.queue_size_bytes,
        .queue_policy = helper::Audio::StreamQueuePolicy::DropNewest,
    };
}

std::expected<AudioEncoderDynamicConfig, std::string> to_audio_encoder_config(const df::AudioCaptureConfig &config)
{
    auto type = to_audio_codec_format(config.type);
    if (!type) {
        return std::unexpected(type.error());
    }

    AudioEncoderDynamicConfig encoder_config{
        .type = type.value(),
        .general = AudioCodecGeneralConfig{
            .channels = config.general.channels,
            .sample_bits = config.general.sample_bits,
            .sample_rate = config.general.sample_rate,
            .frame_duration = config.general.frame_duration,
        },
        .extra = std::monostate{},
        .fetch_interval_ms = config.fetch_interval_ms,
        .fetch_data_size = config.fetch_data_size,
        .enable_afe = config.enable_afe,
        .afe_wake_start_timeout_ms = config.afe_wake_start_timeout_ms,
        .afe_wake_end_timeout_ms = config.afe_wake_end_timeout_ms,
    };
    if (config.opus.has_value()) {
        encoder_config.extra = AudioEncoderExtraConfigOpus{
            .enable_vbr = config.opus->enable_vbr,
            .bitrate = config.opus->bitrate,
        };
    }
    if (encoder_config.type == AudioCodecFormat::PCM) {
        const auto &general = encoder_config.general;
        encoder_config.fetch_data_size = general.frame_duration * general.sample_rate * general.channels *
                                         general.sample_bits / 8 / 1000 + PCM_FETCH_DATA_SIZE_MARGIN;
    }

    return encoder_config;
}

/**
 * @brief Keeps the single physical encoder assigned to at most one capture operation.
 *
 * A provider owns this lease and every operation holds a shared reference to it.
 * Stopping remains part of the lease until the physical encoder has stopped, so a
 * newly opened operation cannot race a teardown from the preceding operation.
 */
class AudioCaptureOperationLease {
public:
    bool try_reserve(const void *operation)
    {
        std::lock_guard lock(mutex_);
        if ((owner_ != nullptr) && (owner_ != operation)) {
            return false;
        }
        if (stopping_) {
            return false;
        }
        owner_ = operation;
        return true;
    }

    bool begin_stop(const void *operation)
    {
        std::lock_guard lock(mutex_);
        if ((owner_ != operation) || stopping_) {
            return false;
        }
        stopping_ = true;
        return true;
    }

    void finish_stop(const void *operation)
    {
        std::lock_guard lock(mutex_);
        if (owner_ != operation) {
            return;
        }
        owner_ = nullptr;
        stopping_ = false;
    }

    void release(const void *operation)
    {
        std::lock_guard lock(mutex_);
        if (owner_ != operation) {
            return;
        }
        owner_ = nullptr;
        stopping_ = false;
    }

    bool owns(const void *operation) const
    {
        std::lock_guard lock(mutex_);
        return (owner_ == operation) && !stopping_;
    }

private:
    mutable std::mutex mutex_;
    const void *owner_ = nullptr;
    bool stopping_ = false;
};

class AudioPlaybackDataFlowOperation final : public df::AudioPlaybackOperation {
public:
    AudioPlaybackDataFlowOperation(AudioDecoder &decoder, std::string provider_id)
        : decoder_(decoder)
        , provider_id_(std::move(provider_id))
    {}

    std::expected<void, std::string> initialize(const df::AudioPlaybackOperationConfig &config)
    {
        if (config.source.name.empty()) {
            return std::unexpected("Audio playback operation source name is empty");
        }
        if ((config.request_output || config.activate_source || config.open_stream) && config.output_name.empty()) {
            return std::unexpected("Audio playback operation output name is empty");
        }
        if (config.activate_source && !config.request_output) {
            return std::unexpected("Audio playback activation requires an output request");
        }
        if (config.open_stream && !config.request_output) {
            return std::unexpected("Audio playback stream opening requires an output request");
        }

        source_name_ = config.source.name;
        auto register_result = decoder_.register_source(to_audio_source_info(config.source));
        if (!register_result) {
            return std::unexpected(register_result.error());
        }
        source_id_ = register_result.value();

        if (config.request_output) {
            auto request_result = request_output(config.output_name);
            if (!request_result) {
                cleanup();
                return std::unexpected(request_result.error());
            }
        }
        if (config.activate_source) {
            auto active_result = set_active_source(config.output_name);
            if (!active_result) {
                cleanup();
                return std::unexpected(active_result.error());
            }
        }
        if (config.open_stream) {
            auto open_result = open_stream(config.output_name, config.stream);
            if (!open_result) {
                cleanup();
                return std::unexpected(open_result.error());
            }
        }

        return {};
    }

    std::vector<df::OutputInfo> get_outputs() const override
    {
        std::vector<df::OutputInfo> outputs;
        for (const auto &output : decoder_.get_outputs()) {
            outputs.push_back(to_dataflow_output_info(output, provider_id_));
        }
        return outputs;
    }

    std::expected<void, std::string> request_output(std::string_view output_name) override
    {
        return decoder_.request_output(source_id_, output_name);
    }

    std::expected<void, std::string> release_output(std::string_view output_name) override
    {
        return decoder_.release_output(source_id_, output_name);
    }

    std::expected<void, std::string> set_active_source(std::string_view output_name) override
    {
        return decoder_.set_active_source(output_name, source_name_);
    }

    std::expected<std::string, std::string> get_active_source(std::string_view output_name) const override
    {
        return decoder_.get_active_source(output_name);
    }

    std::expected<void, std::string> open_stream(
        std::string_view output_name, const df::AudioStreamConfig &config
    ) override
    {
        auto stream_config = to_audio_stream_config(config);
        if (!stream_config) {
            return std::unexpected(stream_config.error());
        }
        return decoder_.open_stream(source_id_, output_name, stream_config.value());
    }

    std::expected<void, std::string> close_stream(std::string_view output_name) override
    {
        return decoder_.close_stream(source_id_, output_name);
    }

    df::AudioWriteResult write_copy(
        std::string_view output_name, std::span<const uint8_t> data, uint32_t timeout_ms
    ) override
    {
        if (!is_available()) {
            return df::AudioWriteResult::Closed;
        }
        return to_dataflow_audio_write_result(
                   decoder_.write_stream(source_id_, output_name, RawBuffer(data.data(), data.size()), timeout_ms)
               );
    }

    df::AudioWriteResult write_borrowed(
        std::string_view output_name, std::span<const uint8_t> data, ReleaseCallback on_release, uint32_t timeout_ms
    ) override
    {
        if (!is_available()) {
            if (on_release) {
                on_release(df::AudioWriteResult::Closed);
            }
            return df::AudioWriteResult::Closed;
        }

        auto released = std::make_shared<std::atomic<bool>>(false);
        auto release_callback = [released, on_release = std::move(on_release)](AudioWriteResult result) {
            if (!released->exchange(true) && on_release) {
                on_release(to_dataflow_audio_write_result(result));
            }
        };
        const auto result = decoder_.write_stream_borrowed(
                                source_id_, output_name, RawBuffer(data.data(), data.size()), release_callback, timeout_ms
                            );
        const auto dataflow_result = to_dataflow_audio_write_result(result);
        if (dataflow_result != df::AudioWriteResult::Written) {
            release_callback(result);
        }
        return dataflow_result;
    }

    bool is_stream_drained(std::string_view output_name) const override
    {
        return decoder_.is_stream_drained(source_id_, output_name);
    }

protected:
    void on_close() override
    {
        cleanup();
    }

private:
    void cleanup()
    {
        if (source_id_ == 0) {
            return;
        }
        auto unregister_result = decoder_.unregister_source(source_id_);
        if (!unregister_result) {
            BROOKESIA_LOGW("Failed to unregister DataFlow audio source '%1%': %2%", source_name_,
                           unregister_result.error());
        }
        source_id_ = 0;
    }

    AudioDecoder &decoder_;
    std::string provider_id_;
    std::string source_name_;
    uint32_t source_id_ = 0;
};

} // namespace

class AudioCaptureDataFlowOperation final
    : public df::AudioCaptureOperation
    , public std::enable_shared_from_this<AudioCaptureDataFlowOperation> {
public:
    AudioCaptureDataFlowOperation(AudioEncoder &encoder, std::shared_ptr<AudioCaptureOperationLease> lease)
        : encoder_(encoder)
        , lease_(std::move(lease))
    {}

    ~AudioCaptureDataFlowOperation() override
    {
        stop();
    }

    std::expected<void, std::string> reserve()
    {
        std::lock_guard lock(operation_mutex_);
        return reserve_locked();
    }

    std::expected<void, std::string> start(const df::AudioCaptureConfig &config) override
    {
        auto encoder_config = to_audio_encoder_config(config);
        if (!encoder_config) {
            return std::unexpected(encoder_config.error());
        }

        std::lock_guard lock(operation_mutex_);
        auto reserve_result = reserve_locked();
        if (!reserve_result) {
            return std::unexpected(reserve_result.error());
        }

        if (encoder_.encoder_iface_ && encoder_.encoder_iface_->is_started()) {
            return {};
        }
        if (!encoder_.start_encoder(encoder_config.value())) {
            lease_->release(this);
            return std::unexpected("Failed to start audio encoder");
        }
        started_by_operation_ = true;
        return {};
    }

    void stop() override
    {
        std::lock_guard lock(operation_mutex_);
        if (!lease_->begin_stop(this)) {
            return;
        }
        if (started_by_operation_) {
            encoder_.stop_encoder();
        }
        started_by_operation_ = false;
        lease_->finish_stop(this);
    }

    void pause() override
    {
        if (!owns_capture_operation()) {
            return;
        }
        auto result = encoder_.function_pause();
        if (!result) {
            BROOKESIA_LOGW("Failed to pause DataFlow audio capture: %1%", result.error());
        }
    }

    void resume() override
    {
        if (!owns_capture_operation()) {
            return;
        }
        auto result = encoder_.function_resume();
        if (!result) {
            BROOKESIA_LOGW("Failed to resume DataFlow audio capture: %1%", result.error());
        }
    }

    void pause_wake_end() override
    {
        if (!owns_capture_operation()) {
            return;
        }
        auto result = encoder_.function_pause_wake_end();
        if (!result) {
            BROOKESIA_LOGW("Failed to pause DataFlow audio capture WakeEnd: %1%", result.error());
        }
    }

    void resume_wake_end() override
    {
        if (!owns_capture_operation()) {
            return;
        }
        auto result = encoder_.function_resume_wake_end();
        if (!result) {
            BROOKESIA_LOGW("Failed to resume DataFlow audio capture WakeEnd: %1%", result.error());
        }
    }

    bool is_started() const override
    {
        return owns_capture_operation() && encoder_.encoder_iface_ && encoder_.encoder_iface_->is_started();
    }

    bool is_paused() const override
    {
        return owns_capture_operation() && encoder_.encoder_iface_ && encoder_.encoder_iface_->is_paused();
    }

    std::vector<std::string> get_afe_wake_words() const override
    {
        if (!owns_capture_operation() || !encoder_.encoder_iface_) {
            return {};
        }
        return encoder_.encoder_iface_->get_afe_wake_words();
    }

    lib_utils::connection connect_data(DataCallback callback) override
    {
        auto weak_operation = weak_from_this();
        auto slot = [weak_operation, callback = std::move(callback)](const RawBuffer & data) {
            auto operation = weak_operation.lock();
            if (operation && operation->owns_capture_operation() && callback && (data.data_ptr != nullptr) &&
                    (data.data_size > 0)) {
                callback(std::span<const uint8_t>(data.data_ptr, data.data_size));
            }
        };
        return encoder_.connect_encoded_data(slot);
    }

    lib_utils::connection connect_afe_event(AfeEventCallback callback) override
    {
        auto weak_operation = weak_from_this();
        auto slot = [weak_operation, callback = std::move(callback)](AudioAFE_Event event) {
            auto operation = weak_operation.lock();
            if (operation && operation->owns_capture_operation() && callback) {
                callback(to_dataflow_audio_afe_event(event));
            }
        };
        return encoder_.afe_event_signal_.connect(slot);
    }

protected:
    void on_close() override
    {
        stop();
    }

private:
    std::expected<void, std::string> reserve_locked()
    {
        if (!is_available()) {
            return std::unexpected("Audio capture operation is no longer available");
        }
        if (!lease_->try_reserve(this)) {
            return std::unexpected("Audio capture operation is busy");
        }
        return {};
    }

    bool owns_capture_operation() const
    {
        return lease_->owns(this);
    }

    AudioEncoder &encoder_;
    std::shared_ptr<AudioCaptureOperationLease> lease_;
    mutable std::mutex operation_mutex_;
    bool started_by_operation_ = false;
};

namespace {

class AudioPlaybackDataFlowProvider final : public df::DataFlowProvider {
public:
    AudioPlaybackDataFlowProvider(AudioDecoder &decoder, int id)
        : decoder_(decoder)
        , provider_id_(make_provider_id("decoder", id))
        , service_name_(decoder.get_attributes().name)
    {}

    df::ProviderInfo get_provider_info() const override
    {
        return {
            .id = provider_id_,
            .service_name = service_name_,
            .description = "Audio decoder playback data-flow provider.",
            .models = {df::Model::AudioPlayback},
        };
    }

    std::vector<df::OutputInfo> list_outputs(df::Model model) const override
    {
        if (model != df::Model::AudioPlayback) {
            return {};
        }

        std::vector<df::OutputInfo> outputs;
        for (const auto &output : decoder_.get_outputs()) {
            outputs.push_back(to_dataflow_output_info(output, provider_id_));
        }
        if (outputs.empty()) {
            outputs.push_back({
                .provider_id = provider_id_,
                .name = DECODER_OUTPUT_NAME,
                .role = DECODER_OUTPUT_ROLE,
                .sample_bits = 16,
                .model = df::Model::AudioPlayback,
            });
        }
        return outputs;
    }

    std::expected<std::shared_ptr<df::AudioPlaybackOperation>, std::string> open_audio_playback_operation(
        const df::AudioPlaybackOperationConfig &config
    ) override
    {
        auto operation = std::make_shared<AudioPlaybackDataFlowOperation>(decoder_, provider_id_);
        auto result = operation->initialize(config);
        if (!result) {
            return std::unexpected(result.error());
        }
        return operation;
    }

private:
    AudioDecoder &decoder_;
    std::string provider_id_;
    std::string service_name_;
};

class AudioCaptureDataFlowProvider final : public df::DataFlowProvider {
public:
    AudioCaptureDataFlowProvider(AudioEncoder &encoder, int id)
        : encoder_(encoder)
        , lease_(std::make_shared<AudioCaptureOperationLease>())
        , provider_id_(make_provider_id("encoder", id))
        , service_name_(encoder.get_attributes().name)
    {}

    df::ProviderInfo get_provider_info() const override
    {
        return {
            .id = provider_id_,
            .service_name = service_name_,
            .description = "Audio encoder capture data-flow provider.",
            .models = {df::Model::AudioCapture},
        };
    }

    std::vector<df::OutputInfo> list_outputs(df::Model) const override
    {
        return {};
    }

    std::expected<std::shared_ptr<df::AudioCaptureOperation>, std::string> open_audio_capture_operation(
        const df::AudioCaptureOperationConfig &
    ) override
    {
        auto operation = std::make_shared<AudioCaptureDataFlowOperation>(encoder_, lease_);
        auto reserve_result = operation->reserve();
        if (!reserve_result) {
            return std::unexpected(reserve_result.error());
        }
        return operation;
    }

private:
    AudioEncoder &encoder_;
    std::shared_ptr<AudioCaptureOperationLease> lease_;
    std::string provider_id_;
    std::string service_name_;
};

} // namespace

std::expected<dataflow::ProviderRegistration, std::string> register_audio_decoder_dataflow_provider(
    AudioDecoder &decoder, int id
)
{
    auto provider = std::make_shared<AudioPlaybackDataFlowProvider>(decoder, id);
    return ServiceManager::get_instance().get_dataflow_registry().register_provider(std::move(provider));
}

std::expected<dataflow::ProviderRegistration, std::string> register_audio_encoder_dataflow_provider(
    AudioEncoder &encoder, int id
)
{
    auto provider = std::make_shared<AudioCaptureDataFlowProvider>(encoder, id);
    return ServiceManager::get_instance().get_dataflow_registry().register_provider(std::move(provider));
}

} // namespace esp_brookesia::service
