/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/audio_impl.hpp"
#include <cstring>

namespace esp_brookesia::service {


AudioDecoder::AudioDecoder(int id)
    : ServiceBase({
    .name = std::string(helper::Audio::DECODER_NAME_PREFIX) + std::to_string(id),
    .description = "Decode and play audio for one configured output instance.",
    .version = make_version(
        BROOKESIA_SERVICE_AUDIO_VER_MAJOR, BROOKESIA_SERVICE_AUDIO_VER_MINOR, BROOKESIA_SERVICE_AUDIO_VER_PATCH
    ),
#if BROOKESIA_SERVICE_AUDIO_ENABLE_WORKER
    .task_scheduler_config = lib_utils::TaskScheduler::StartConfig{
        .worker_configs = {
            lib_utils::ThreadConfig{
                .name = BROOKESIA_SERVICE_AUDIO_WORKER_NAME,
                .core_id = BROOKESIA_SERVICE_AUDIO_WORKER_CORE_ID,
                .priority = BROOKESIA_SERVICE_AUDIO_WORKER_PRIORITY,
                .stack_size = BROOKESIA_SERVICE_AUDIO_WORKER_STACK_SIZE,
                .stack_in_ext = BROOKESIA_SERVICE_AUDIO_WORKER_STACK_IN_EXT,
            }
        },
        .worker_poll_interval_ms = BROOKESIA_SERVICE_AUDIO_WORKER_POLL_INTERVAL_MS,
    }
#endif
})
, id_(id)
{
    std::lock_guard lock(get_decoder_registry_mutex());
    auto &registry = get_decoder_registry();
    if (id_ >= 0 && static_cast<size_t>(id_) >= registry.size()) {
        registry.resize(static_cast<size_t>(id_) + 1, nullptr);
    }
    if (id_ >= 0) {
        registry[static_cast<size_t>(id_)] = this;
    }
}


AudioDecoder::~AudioDecoder()
{
    std::lock_guard lock(get_decoder_registry_mutex());
    auto &registry = get_decoder_registry();
    if (id_ >= 0 && static_cast<size_t>(id_) < registry.size() && registry[static_cast<size_t>(id_)] == this) {
        registry[static_cast<size_t>(id_)] = nullptr;
    }
}


AudioDecoder *AudioDecoder::get_instance(int id)
{
    std::lock_guard lock(get_decoder_registry_mutex());
    auto &registry = get_decoder_registry();
    if (id < 0 || static_cast<size_t>(id) >= registry.size()) {
        return nullptr;
    }
    return registry[static_cast<size_t>(id)];
}


std::vector<AudioOutputInfo> AudioDecoder::get_outputs() const
{
    std::lock_guard lock(decoder_state_mutex_);
    std::vector<AudioOutputInfo> outputs;
    outputs.reserve(outputs_.size());
    for (const auto &output : outputs_) {
        outputs.push_back(output.info);
    }
    return outputs;
}


std::vector<AudioSourceInfo> AudioDecoder::get_sources() const
{
    std::lock_guard lock(decoder_state_mutex_);
    std::vector<AudioSourceInfo> sources;
    sources.reserve(sources_.size());
    for (const auto &[_, source] : sources_) {
        sources.push_back(source.info);
    }
    return sources;
}


std::expected<uint32_t, std::string> AudioDecoder::register_source(AudioSourceInfo source)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (source.name.empty()) {
        return std::unexpected("Audio source name is empty");
    }

    std::lock_guard lock(decoder_state_mutex_);
    if (find_source_by_name_locked(source.name) != nullptr) {
        return std::unexpected("Audio source already registered: " + source.name);
    }

    if (source.id == 0) {
        source.id = next_source_id_++;
    } else if (sources_.contains(source.id)) {
        return std::unexpected("Audio source id already registered");
    }

    const uint32_t source_id = source.id;
    sources_.emplace(source_id, SourceContext{
        .info = std::move(source),
        .requested_outputs = {},
        .streams = {},
    });
    return source_id;
}


std::expected<void, std::string> AudioDecoder::unregister_source(uint32_t source_id)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(decoder_state_mutex_);
    auto source_it = sources_.find(source_id);
    if (source_it == sources_.end()) {
        return std::unexpected("Audio source is not registered");
    }

    for (auto &output : outputs_) {
        if (output.active_source_id == source_id) {
            stop_hal_decoder_locked();
            output.active_source_id = 0;
            emit_active_source_changed(output.info.name, "");
        }
    }

    for (auto &[output_name, stream] : source_it->second.streams) {
        clear_stream_queue_locked(stream);
        emit_source_state_changed(source_it->second.info.name, output_name, AudioSourceState::Released);
    }
    sources_.erase(source_it);
    return {};
}


std::expected<void, std::string> AudioDecoder::unregister_source(std::string_view source_name)
{
    uint32_t source_id = 0;
    {
        std::lock_guard lock(decoder_state_mutex_);
        const auto *source = find_source_by_name_locked(source_name);
        if (source == nullptr) {
            return std::unexpected("Audio source is not registered");
        }
        source_id = source->info.id;
    }
    return unregister_source(source_id);
}


std::expected<void, std::string> AudioDecoder::request_output(uint32_t source_id, std::string_view output_name)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(decoder_state_mutex_);
    auto *source = find_source_by_id_locked(source_id);
    if (source == nullptr) {
        return std::unexpected("Audio source is not registered");
    }
    if (find_output_locked(output_name) == nullptr) {
        return std::unexpected("Audio output is not registered");
    }

    source->requested_outputs.insert(std::string(output_name));
    emit_source_state_changed(source->info.name, std::string(output_name), AudioSourceState::Requested);
    return {};
}


std::expected<void, std::string> AudioDecoder::request_output(
    std::string_view source_name, std::string_view output_name
)
{
    uint32_t source_id = 0;
    {
        std::lock_guard lock(decoder_state_mutex_);
        const auto *source = find_source_by_name_locked(source_name);
        if (source == nullptr) {
            return std::unexpected("Audio source is not registered");
        }
        source_id = source->info.id;
    }
    return request_output(source_id, output_name);
}


std::expected<void, std::string> AudioDecoder::release_output(uint32_t source_id, std::string_view output_name)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(decoder_state_mutex_);
    auto *source = find_source_by_id_locked(source_id);
    auto *output = find_output_locked(output_name);
    if (source == nullptr) {
        return std::unexpected("Audio source is not registered");
    }
    if (output == nullptr) {
        return std::unexpected("Audio output is not registered");
    }

    if (output->active_source_id == source_id) {
        stop_hal_decoder_locked();
        output->active_source_id = 0;
        emit_active_source_changed(output->info.name, "");
    }
    auto stream_it = source->streams.find(std::string(output_name));
    if (stream_it != source->streams.end()) {
        clear_stream_queue_locked(stream_it->second);
        source->streams.erase(stream_it);
    }
    source->requested_outputs.erase(std::string(output_name));
    emit_source_state_changed(source->info.name, std::string(output_name), AudioSourceState::Released);
    return {};
}


std::expected<void, std::string> AudioDecoder::release_output(
    std::string_view source_name, std::string_view output_name
)
{
    uint32_t source_id = 0;
    {
        std::lock_guard lock(decoder_state_mutex_);
        const auto *source = find_source_by_name_locked(source_name);
        if (source == nullptr) {
            return std::unexpected("Audio source is not registered");
        }
        source_id = source->info.id;
    }
    return release_output(source_id, output_name);
}


std::expected<void, std::string> AudioDecoder::set_active_source(
    std::string_view output_name, std::string_view source_name
)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(decoder_state_mutex_);
    auto *output = find_output_locked(output_name);
    auto *source = find_source_by_name_locked(source_name);
    if (output == nullptr) {
        return std::unexpected("Audio output is not registered");
    }
    if (source == nullptr) {
        return std::unexpected("Audio source is not registered");
    }
    if (!source->requested_outputs.contains(std::string(output_name))) {
        return std::unexpected("Audio source has not requested output: " + std::string(output_name));
    }
    if (output->active_source_id == source->info.id) {
        return {};
    }

    if (output->active_source_id != 0) {
        auto *old_source = find_source_by_id_locked(output->active_source_id);
        if (old_source != nullptr) {
            emit_source_state_changed(old_source->info.name, output->info.name, AudioSourceState::Requested);
        }
    }
    stop_hal_decoder_locked();
    clear_all_queues_locked();
    output->active_source_id = source->info.id;
    emit_source_state_changed(source->info.name, output->info.name, AudioSourceState::Granted);
    emit_active_source_changed(output->info.name, source->info.name);
    auto stream_it = source->streams.find(output->info.name);
    if (stream_it != source->streams.end() && stream_it->second.opened) {
        ensure_hal_decoder_for_active_stream_locked(*source, output->info.name);
    }
    return {};
}


std::expected<std::string, std::string> AudioDecoder::get_active_source(std::string_view output_name) const
{
    std::lock_guard lock(decoder_state_mutex_);
    const auto *output = find_output_locked(output_name);
    if (output == nullptr) {
        return std::unexpected("Audio output is not registered");
    }
    const auto *source = find_source_by_id_locked(output->active_source_id);
    return source != nullptr ? source->info.name : std::string();
}


std::expected<void, std::string> AudioDecoder::open_stream(
    uint32_t source_id, std::string_view output_name, const AudioStreamConfig &config
)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(decoder_state_mutex_);
    auto *source = find_source_by_id_locked(source_id);
    if (source == nullptr) {
        return std::unexpected("Audio source is not registered");
    }
    if (find_output_locked(output_name) == nullptr) {
        return std::unexpected("Audio output is not registered");
    }
    if (!source->requested_outputs.contains(std::string(output_name))) {
        return std::unexpected("Audio source has not requested output: " + std::string(output_name));
    }

    auto &stream = source->streams[std::string(output_name)];
    clear_stream_queue_locked(stream);
    stream.config = config;
    if (stream.config.queue_size_bytes == 0) {
        stream.config.queue_size_bytes = DECODER_STREAM_QUEUE_SIZE_DEFAULT;
    }
    stream.opened = true;
    if (is_source_active_locked(source_id, output_name)) {
        if (!ensure_hal_decoder_for_active_stream_locked(*source, std::string(output_name))) {
            return std::unexpected("Failed to start audio decoder stream");
        }
    }
    if (!start_decoder_stream_task_locked()) {
        return std::unexpected("Failed to start decoder stream task");
    }
    return {};
}


std::expected<void, std::string> AudioDecoder::open_stream(
    std::string_view source_name, std::string_view output_name, const AudioStreamConfig &config
)
{
    uint32_t source_id = 0;
    {
        std::lock_guard lock(decoder_state_mutex_);
        const auto *source = find_source_by_name_locked(source_name);
        if (source == nullptr) {
            return std::unexpected("Audio source is not registered");
        }
        source_id = source->info.id;
    }
    return open_stream(source_id, output_name, config);
}


std::expected<void, std::string> AudioDecoder::close_stream(uint32_t source_id, std::string_view output_name)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(decoder_state_mutex_);
    auto *source = find_source_by_id_locked(source_id);
    if (source == nullptr) {
        return std::unexpected("Audio source is not registered");
    }
    auto stream_it = source->streams.find(std::string(output_name));
    if (stream_it == source->streams.end()) {
        return {};
    }
    if (is_source_active_locked(source_id, output_name)) {
        stop_hal_decoder_locked();
    }
    clear_stream_queue_locked(stream_it->second);
    source->streams.erase(stream_it);
    return {};
}


std::expected<void, std::string> AudioDecoder::close_stream(
    std::string_view source_name, std::string_view output_name
)
{
    uint32_t source_id = 0;
    {
        std::lock_guard lock(decoder_state_mutex_);
        const auto *source = find_source_by_name_locked(source_name);
        if (source == nullptr) {
            return std::unexpected("Audio source is not registered");
        }
        source_id = source->info.id;
    }
    return close_stream(source_id, output_name);
}


AudioWriteResult AudioDecoder::write_stream(
    uint32_t source_id, std::string_view output_name, const RawBuffer &data, uint32_t timeout_ms
)
{
    if ((data.data_ptr == nullptr) || (data.data_size == 0)) {
        return AudioWriteResult::DroppedInvalidData;
    }

    std::unique_lock lock(decoder_state_mutex_);
    StreamContext *stream = nullptr;
    auto reserve_result = reserve_stream_queue_space_locked(
                              lock, source_id, output_name, data.data_size, timeout_ms, stream
                          );
    if (reserve_result != AudioWriteResult::Written) {
        return reserve_result;
    }

    StreamContext::Chunk chunk = {};
    chunk.owned_data.assign(data.data_ptr, data.data_ptr + data.data_size);
    stream->queued_bytes += chunk.owned_data.size();
    stream->queue.emplace_back(std::move(chunk));
    return AudioWriteResult::Written;
}


AudioWriteResult AudioDecoder::write_stream(
    std::string_view source_name, std::string_view output_name, const RawBuffer &data, uint32_t timeout_ms
)
{
    uint32_t source_id = 0;
    {
        std::lock_guard lock(decoder_state_mutex_);
        const auto *source = find_source_by_name_locked(source_name);
        if (source == nullptr) {
            return AudioWriteResult::Error;
        }
        source_id = source->info.id;
    }
    return write_stream(source_id, output_name, data, timeout_ms);
}


AudioWriteResult AudioDecoder::write_stream_borrowed(
    uint32_t source_id, std::string_view output_name, const RawBuffer &data,
    StreamBufferReleaseCallback release_callback, uint32_t timeout_ms
)
{
    if ((data.data_ptr == nullptr) || (data.data_size == 0)) {
        return AudioWriteResult::DroppedInvalidData;
    }
    if (!release_callback) {
        return AudioWriteResult::DroppedInvalidData;
    }

    StreamContext::Chunk chunk = {};
    chunk.borrowed_data = data;
    chunk.release_callback = std::move(release_callback);
    chunk.borrowed = true;
    return enqueue_stream_chunk_locked(source_id, output_name, std::move(chunk), timeout_ms);
}


AudioWriteResult AudioDecoder::write_stream_borrowed(
    std::string_view source_name, std::string_view output_name, const RawBuffer &data,
    StreamBufferReleaseCallback release_callback, uint32_t timeout_ms
)
{
    uint32_t source_id = 0;
    {
        std::lock_guard lock(decoder_state_mutex_);
        const auto *source = find_source_by_name_locked(source_name);
        if (source == nullptr) {
            return AudioWriteResult::Error;
        }
        source_id = source->info.id;
    }
    return write_stream_borrowed(source_id, output_name, data, std::move(release_callback), timeout_ms);
}


bool AudioDecoder::is_stream_drained(uint32_t source_id, std::string_view output_name) const
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(decoder_state_mutex_);
    const auto *source = find_source_by_id_locked(source_id);
    if (source == nullptr) {
        return false;
    }
    auto stream_it = source->streams.find(std::string(output_name));
    if (stream_it == source->streams.end()) {
        return false;
    }

    return stream_it->second.opened && stream_it->second.queue.empty() && (stream_it->second.queued_bytes == 0);
}


bool AudioDecoder::is_stream_drained(std::string_view source_name, std::string_view output_name) const
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(decoder_state_mutex_);
    const auto *source = find_source_by_name_locked(source_name);
    if (source == nullptr) {
        return false;
    }
    auto stream_it = source->streams.find(std::string(output_name));
    if (stream_it == source->streams.end()) {
        return false;
    }

    return stream_it->second.opened && stream_it->second.queue.empty() && (stream_it->second.queued_bytes == 0);
}


bool AudioDecoder::on_init()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_LOGI(
        "Version: %1%.%2%.%3%", BROOKESIA_SERVICE_AUDIO_VER_MAJOR, BROOKESIA_SERVICE_AUDIO_VER_MINOR,
        BROOKESIA_SERVICE_AUDIO_VER_PATCH
    );

    auto registration = register_audio_decoder_dataflow_provider(*this, id_);
    if (!registration) {
        BROOKESIA_LOGE("Failed to register audio decoder DataFlow provider: %1%", registration.error());
        return false;
    }
    dataflow_provider_registration_.emplace(std::move(registration.value()));

    return true;
}


void AudioDecoder::on_deinit()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    dataflow_provider_registration_.reset();
}


bool AudioDecoder::on_start()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    auto decoder_handle = hal::acquire_interface<hal::audio::DecoderIface>(
                              hal::audio::DecoderIface::get_default_instance_name(id_)
                          );
    auto decoder_iface = decoder_handle.get();
    BROOKESIA_CHECK_NULL_RETURN(decoder_iface, false, "Failed to get audio decoder interface");
    std::lock_guard lock(decoder_state_mutex_);
    decoder_iface_ = std::move(decoder_handle);
    outputs_ = {
        OutputContext{
            .info = AudioOutputInfo{
                .id = 0,
                .name = DECODER_OUTPUT_NAME,
                .role = DECODER_OUTPUT_ROLE,
                .sample_rates = {},
                .channels = {},
                .sample_bits = {16},
            },
        },
    };

    return true;
}


void AudioDecoder::on_stop()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    ServiceManager::get_instance().get_dataflow_registry().invalidate_provider_operations(
        make_dataflow_provider_id("decoder", id_)
    );
    stop_decoder_stream_task();
    std::lock_guard lock(decoder_state_mutex_);
    stop_hal_decoder_locked();
    clear_all_queues_locked();
    sources_.clear();
    outputs_.clear();
    decoder_iface_.reset();
}


std::expected<boost::json::array, std::string> AudioDecoder::function_get_outputs()
{
    return BROOKESIA_DESCRIBE_TO_JSON(get_outputs()).as_array();
}


std::expected<boost::json::array, std::string> AudioDecoder::function_get_sources()
{
    return BROOKESIA_DESCRIBE_TO_JSON(get_sources()).as_array();
}


std::expected<double, std::string> AudioDecoder::function_register_source(const boost::json::object &source_json)
{
    AudioSourceInfo source;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(source_json, source)) {
        return std::unexpected("Failed to parse audio source info");
    }
    auto result = register_source(std::move(source));
    if (!result) {
        return std::unexpected(result.error());
    }
    return static_cast<double>(result.value());
}


std::expected<void, std::string> AudioDecoder::function_unregister_source(const std::string &source_name)
{
    return unregister_source(source_name);
}


std::expected<void, std::string> AudioDecoder::function_request_output(
    const std::string &source_name, const std::string &output_name
)
{
    return request_output(source_name, output_name);
}


std::expected<void, std::string> AudioDecoder::function_release_output(
    const std::string &source_name, const std::string &output_name
)
{
    return release_output(source_name, output_name);
}


std::expected<void, std::string> AudioDecoder::function_set_active_source(
    const std::string &output_name, const std::string &source_name
)
{
    return set_active_source(output_name, source_name);
}


std::expected<std::string, std::string> AudioDecoder::function_get_active_source(const std::string &output_name)
{
    return get_active_source(output_name);
}


std::expected<void, std::string> AudioDecoder::function_open_stream(
    const std::string &source_name, const std::string &output_name, const boost::json::object &config
)
{
    AudioStreamConfig stream_config;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(config, stream_config)) {
        return std::unexpected("Failed to parse audio stream config");
    }
    return open_stream(source_name, output_name, stream_config);
}


std::expected<void, std::string> AudioDecoder::function_close_stream(
    const std::string &source_name, const std::string &output_name
)
{
    return close_stream(source_name, output_name);
}


AudioDecoder::SourceContext *AudioDecoder::find_source_by_id_locked(uint32_t source_id)
{
    auto it = sources_.find(source_id);
    return it != sources_.end() ? &it->second : nullptr;
}


const AudioDecoder::SourceContext *AudioDecoder::find_source_by_id_locked(uint32_t source_id) const
{
    auto it = sources_.find(source_id);
    return it != sources_.end() ? &it->second : nullptr;
}


AudioDecoder::SourceContext *AudioDecoder::find_source_by_name_locked(std::string_view source_name)
{
    for (auto &[_, source] : sources_) {
        if (source.info.name == source_name) {
            return &source;
        }
    }
    return nullptr;
}


const AudioDecoder::SourceContext *AudioDecoder::find_source_by_name_locked(std::string_view source_name) const
{
    for (const auto &[_, source] : sources_) {
        if (source.info.name == source_name) {
            return &source;
        }
    }
    return nullptr;
}


AudioDecoder::OutputContext *AudioDecoder::find_output_locked(std::string_view output_name)
{
    for (auto &output : outputs_) {
        if (output.info.name == output_name) {
            return &output;
        }
    }
    return nullptr;
}


const AudioDecoder::OutputContext *AudioDecoder::find_output_locked(std::string_view output_name) const
{
    for (const auto &output : outputs_) {
        if (output.info.name == output_name) {
            return &output;
        }
    }
    return nullptr;
}


bool AudioDecoder::is_source_active_locked(uint32_t source_id, std::string_view output_name) const
{
    const auto *output = find_output_locked(output_name);
    return (output != nullptr) && (output->active_source_id == source_id);
}


bool AudioDecoder::ensure_hal_decoder_for_active_stream_locked(SourceContext &source, const std::string &output_name)
{
    auto stream_it = source.streams.find(output_name);
    if (stream_it == source.streams.end() || !stream_it->second.opened) {
        return true;
    }

    std::lock_guard hal_lock(decoder_hal_mutex_);
    if ((active_hal_source_id_ == source.info.id) && (active_hal_output_name_ == output_name) &&
            decoder_iface_ && decoder_iface_->is_started()) {
        return true;
    }
    if (decoder_iface_ && decoder_iface_->is_started()) {
        decoder_iface_->stop();
    }
    if (!decoder_iface_) {
        return false;
    }
    const auto &config = stream_it->second.config;
    AudioDecoderDynamicConfig decoder_config{
        .type = config.type,
        .general = config.general,
    };
    if (!decoder_iface_->start(decoder_config)) {
        active_hal_source_id_ = 0;
        active_hal_output_name_.clear();
        return false;
    }
    active_hal_source_id_ = source.info.id;
    active_hal_output_name_ = output_name;
    active_hal_config_ = config;
    return true;
}


AudioWriteResult AudioDecoder::reserve_stream_queue_space_locked(
    std::unique_lock<std::mutex> &lock, uint32_t source_id, std::string_view output_name, size_t data_size,
    uint32_t timeout_ms, StreamContext *&stream
)
{
    auto find_stream = [&]() -> AudioWriteResult {
        stream = nullptr;
        auto *source = find_source_by_id_locked(source_id);
        if (source == nullptr)
        {
            return AudioWriteResult::Error;
        }
        if (!is_source_active_locked(source_id, output_name))
        {
            return AudioWriteResult::DroppedNotActive;
        }
        auto stream_it = source->streams.find(std::string(output_name));
        if ((stream_it == source->streams.end()) || !stream_it->second.opened)
        {
            return AudioWriteResult::Error;
        }
        stream = &stream_it->second;
        if (data_size > stream->config.queue_size_bytes)
        {
            return AudioWriteResult::DroppedQueueFull;
        }
        return AudioWriteResult::Written;
    };

    auto has_space = [&]() {
        return (stream != nullptr) && ((stream->queued_bytes + data_size) <= stream->config.queue_size_bytes);
    };

    auto result = find_stream();
    if (result != AudioWriteResult::Written) {
        return result;
    }
    if (has_space()) {
        return AudioWriteResult::Written;
    }
    if (timeout_ms == 0) {
        return AudioWriteResult::DroppedQueueFull;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        if (decoder_queue_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            result = find_stream();
            if (result != AudioWriteResult::Written) {
                return result;
            }
            return has_space() ? AudioWriteResult::Written : AudioWriteResult::DroppedQueueFull;
        }

        result = find_stream();
        if (result != AudioWriteResult::Written) {
            return result;
        }
        if (has_space()) {
            return AudioWriteResult::Written;
        }
    }
}


AudioWriteResult AudioDecoder::enqueue_stream_chunk_locked(
    uint32_t source_id, std::string_view output_name, StreamContext::Chunk &&chunk, uint32_t timeout_ms
)
{
    const size_t data_size = chunk.size();
    if ((chunk.data() == nullptr) || (data_size == 0)) {
        return AudioWriteResult::DroppedInvalidData;
    }

    std::unique_lock lock(decoder_state_mutex_);
    StreamContext *stream = nullptr;
    auto reserve_result = reserve_stream_queue_space_locked(
                              lock, source_id, output_name, data_size, timeout_ms, stream
                          );
    if (reserve_result != AudioWriteResult::Written) {
        return reserve_result;
    }

    stream->queued_bytes += data_size;
    stream->queue.emplace_back(std::move(chunk));
    return AudioWriteResult::Written;
}


void AudioDecoder::clear_stream_queue_locked(StreamContext &stream)
{
    while (!stream.queue.empty()) {
        auto chunk = std::move(stream.queue.front());
        stream.queue.pop_front();
        chunk.release(AudioWriteResult::DroppedNotActive);
    }
    stream.queue.clear();
    stream.queued_bytes = 0;
    decoder_queue_cv_.notify_all();
}


void AudioDecoder::clear_all_queues_locked()
{
    for (auto &[_, source] : sources_) {
        for (auto &[__, stream] : source.streams) {
            clear_stream_queue_locked(stream);
        }
    }
}


void AudioDecoder::stop_hal_decoder_locked()
{
    std::lock_guard hal_lock(decoder_hal_mutex_);
    if (decoder_iface_ && decoder_iface_->is_started()) {
        decoder_iface_->stop();
    }
    active_hal_source_id_ = 0;
    active_hal_output_name_.clear();
    active_hal_config_ = {};
}


bool AudioDecoder::start_decoder_stream_task_locked()
{
    if (decoder_stream_task_id_ != 0) {
        return true;
    }
    auto scheduler = get_task_scheduler();
    BROOKESIA_CHECK_NULL_RETURN(scheduler, false, "Task scheduler is not available");
    auto result = scheduler->post_periodic([this]() {
        return decoder_stream_task();
    }, DECODER_STREAM_DRAIN_INTERVAL_MS, &decoder_stream_task_id_, get_call_task_group());
    BROOKESIA_CHECK_FALSE_RETURN(result, false, "Failed to schedule decoder stream task");
    return true;
}


void AudioDecoder::stop_decoder_stream_task()
{
    if (decoder_stream_task_id_ == 0) {
        return;
    }
    auto scheduler = get_task_scheduler();
    if (scheduler) {
        scheduler->cancel(decoder_stream_task_id_);
    }
    decoder_stream_task_id_ = 0;
}


bool AudioDecoder::decoder_stream_task()
{
    StreamContext::Chunk chunk;
    bool stream_drained = false;
    std::string drained_source_name;
    std::string drained_output_name;
    {
        std::lock_guard lock(decoder_state_mutex_);

        /* LOCAL FIX (2026-10-31): 播放器侧预卷 v2——TTS 服务器按 ~1x 节奏推流，
         * 块间生成停顿(0.3-0.75s)期间队列无缓冲，逐块顿挫(听感"有点卡")。
         * armed 期间先攒够 1.5s 缓冲再开始播放，块间停顿被缓冲吸收；
         * 预卷等待超时(1.8s)则提前播放已有缓冲，防短回复/回复结束死等。
         * v2 关键修正：分片喂入下帧间真空会误触发 stream_drained→re-armed，
         * 每帧之间都重新攒 1.8s → 播放被切成"0.4s 有声+1.8s 静音"断续。
         * 故 drained 后进入 1s 宽限期：期内来数据=同回合续播(取消预卷)，
         * 期后才视为新回合(正常预卷)。初始 grace 满=首回合正常预卷。 */
        if (stream_preload_armed_) {
            bool found_data = false;
            size_t queued_bytes = 0;
            for (auto &output : outputs_) {
                if (output.active_source_id == 0) {
                    continue;
                }
                auto *source = find_source_by_id_locked(output.active_source_id);
                if (source == nullptr) {
                    continue;
                }
                auto sit = source->streams.find(output.info.name);
                if ((sit == source->streams.end()) || sit->second.queue.empty()) {
                    continue;
                }
                found_data = true;
                queued_bytes = sit->second.queued_bytes;
                break;
            }
            if (found_data) {
                if (stream_preload_grace_ticks_ < DECODER_STREAM_PRELOAD_GRACE_TICKS) {
                    /* 回合内真空后快速续流 → 取消预卷直接续播 */
                    stream_preload_armed_ = false;
                } else if (queued_bytes < DECODER_STREAM_PRELOAD_BYTES) {
                    if (++stream_preload_wait_ticks_ >= DECODER_STREAM_PRELOAD_TIMEOUT_TICKS) {
                        stream_preload_armed_ = false;
                        BROOKESIA_LOGW(
                            "AUD_DEC: preload timeout play now (q=%d bytes)", (int)queued_bytes
                        );
                    } else {
                        return true; /* 继续攒缓冲，本 tick 不消费 */
                    }
                } else {
                    stream_preload_armed_ = false; /* 攒够，开始播放 */
                    BROOKESIA_LOGI(
                        "AUD_DEC: preload full play now (q=%d bytes)", (int)queued_bytes
                    );
                }
            } else if (stream_preload_grace_ticks_ < DECODER_STREAM_PRELOAD_GRACE_TICKS) {
                stream_preload_grace_ticks_++; /* 无数据，宽限期计时 */
                if (stream_preload_grace_ticks_ == DECODER_STREAM_PRELOAD_GRACE_TICKS) {
                    BROOKESIA_LOGI("AUD_DEC: grace expired -> next data re-preloads");
                }
            }
        }

        for (auto &output : outputs_) {
            if (output.active_source_id == 0) {
                continue;
            }
            auto *source = find_source_by_id_locked(output.active_source_id);
            if (source == nullptr) {
                continue;
            }
            auto stream_it = source->streams.find(output.info.name);
            if ((stream_it == source->streams.end()) || stream_it->second.queue.empty()) {
                continue;
            }
            chunk = std::move(stream_it->second.queue.front());
            stream_it->second.queue.pop_front();
            stream_it->second.queued_bytes -= chunk.size();
            stream_underrun_ticks_ = 0; /* v14: 有真数据，重置欠载静音计数 */
            /* LOCAL FIX: 缓存最近 0.5s 音频作欠载兜底(尾音拉伸) */
            if ((chunk.size() > 0) && (chunk.data() != nullptr)) {
                if (tail_pcm_len_ + chunk.size() > DECODER_STREAM_TAIL_BUFFER_BYTES) {
                    size_t drop = tail_pcm_len_ + chunk.size() - DECODER_STREAM_TAIL_BUFFER_BYTES;
                    if (drop >= tail_pcm_len_) {
                        tail_pcm_len_ = 0;
                    } else {
                        memmove(tail_pcm_, tail_pcm_ + drop, tail_pcm_len_ - drop);
                        tail_pcm_len_ -= drop;
                    }
                }
                memcpy(tail_pcm_ + tail_pcm_len_, chunk.data(), chunk.size());
                tail_pcm_len_ += chunk.size();
                tail_stretch_rounds_ = 0; /* 有真数据，重置拉伸 */
            }
            /* LOCAL FIX (2026-11-01): 播放侧诊断——每 16 块(≈2s)打一次队列水位，
             * 抓包可精确还原出队节奏与"开头卡"时队列是否吃紧 */
            static uint32_t s_deq_log_count = 0;
            if ((++s_deq_log_count % 16) == 0) {
                BROOKESIA_LOGI("AUD_DEC: deq q=%d", (int)stream_it->second.queued_bytes);
            }
            if (stream_it->second.queue.empty() && (stream_it->second.queued_bytes == 0)) {
                stream_drained = true;
                drained_source_name = source->info.name;
                drained_output_name = output.info.name;
            }
            decoder_queue_cv_.notify_all();
            break;
        }
    }

    if ((chunk.data() == nullptr) || (chunk.size() == 0)) {
        /* LOCAL FIX: 欠载兜底——队列空时把最近 1.0s 尾音重采样 1.25x 喂入，
         * 覆盖服务器块间生成停顿(≤0.8s)，静音间隙变成连续拖音。
         * v14: 短真空(<400ms)静音过渡——重复尾音造成明显"卡顿"听感
         * (v13 抓包实证开头段 1.76s 真空打穿 48KB 预卷后 stretch 拖音=用户听到
         * 的"前面卡顿")；≥400ms 长真空才拉伸，防整段静音。 */
        std::lock_guard hal_lock(decoder_hal_mutex_);
        if (decoder_iface_ && decoder_iface_->is_started() && (tail_pcm_len_ > 0)
            && (tail_stretch_rounds_ < DECODER_STREAM_TAIL_STRETCH_MAX_ROUNDS)
            && !stream_preload_armed_) {
            if (stream_underrun_ticks_ < DECODER_STREAM_UNDERRUN_STRETCH_MIN_TICKS) {
                stream_underrun_ticks_++;
                if (stream_underrun_ticks_ == 1) {
                    BROOKESIA_LOGW("AUD_DEC: underrun silence start (queue empty)");
                }
                return true; /* 短真空静音过渡，不播重复尾音 */
            }
            stream_underrun_ticks_ = 0;
            /* v5: 仅在播放中(非预卷/非回合间隙)欠载时兜底拉伸；
             * drained 后 armed 期间队列空是正常回合结束/等待，不得播重复尾音。 */
            size_t in_samples = tail_pcm_len_ / sizeof(int16_t);
            int16_t *src = reinterpret_cast<int16_t *>(tail_pcm_);
            int16_t *dst = reinterpret_cast<int16_t *>(tail_stretch_out_);
            size_t out_samples = 0;
            size_t out_cap = DECODER_STREAM_TAIL_STRETCH_OUT_BYTES / sizeof(int16_t);
            for (size_t i = 0; (i + 1) < in_samples; i++) {
                int32_t a = src[i];
                int32_t b = src[i + 1];
                for (int j = 0; j < 5; j++) {
                    if (out_samples >= out_cap) {
                        break;
                    }
                    dst[out_samples++] = static_cast<int16_t>((a * (4 - j) + b * j + 2) / 4);
                }
            }
            size_t out_bytes = out_samples * sizeof(int16_t);
            if ((out_bytes > 0) && decoder_iface_->feed_data(tail_stretch_out_, out_bytes)) {
                tail_stretch_rounds_++;
                BROOKESIA_LOGW(
                    "AUD_DEC: underrun stretch round=%d out=%d bytes",
                    tail_stretch_rounds_, (int)out_bytes
                );
            }
        }
        return true;
    }

    std::lock_guard hal_lock(decoder_hal_mutex_);
    if (!decoder_iface_ || !decoder_iface_->is_started()) {
        chunk.release(AudioWriteResult::Error);
        return true;
    }
    if (!decoder_iface_->feed_data(chunk.data(), chunk.size())) {
        BROOKESIA_LOGW("Failed to feed audio decoder stream data");
        chunk.release(AudioWriteResult::Error);
        return true;
    }
    chunk.release(AudioWriteResult::Written);
    if (stream_drained) {
        /* LOCAL FIX: 回合播完，下一回合重新预卷；进入 1s 宽限期
         * (期内来数据=同回合续播，不预卷；期后才预卷新回合) */
        {
            std::lock_guard lock(decoder_state_mutex_);
            stream_preload_armed_ = true;
            stream_preload_wait_ticks_ = 0;
            stream_preload_grace_ticks_ = 0;
        }
        BROOKESIA_LOGI("AUD_DEC: stream drained -> armed (grace 0)");
        emit_stream_drained(drained_source_name, drained_output_name);
    }
    return true;
}


void AudioDecoder::emit_source_state_changed(
    const std::string &source_name, const std::string &output_name, AudioSourceState state
)
{
    source_state_changed_signal_(source_name, output_name, state);
}


void AudioDecoder::emit_active_source_changed(const std::string &output_name, const std::string &source_name)
{
    active_source_changed_signal_(output_name, source_name);
}


void AudioDecoder::emit_stream_drained(const std::string &source_name, const std::string &output_name)
{
    stream_drained_signal_(source_name, output_name);
}
}
