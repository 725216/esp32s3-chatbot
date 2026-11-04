/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/audio_impl.hpp"

namespace esp_brookesia::service {



bool AudioEncoder::on_init()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    {
        std::lock_guard lock(get_encoder_registry_mutex());
        auto &registry = get_encoder_registry();
        if (id_ >= 0 && static_cast<size_t>(id_) >= registry.size()) {
            registry.resize(static_cast<size_t>(id_) + 1, nullptr);
        }
        if (id_ >= 0) {
            registry[static_cast<size_t>(id_)] = this;
        }
    }

    BROOKESIA_LOGI(
        "Version: %1%.%2%.%3%", BROOKESIA_SERVICE_AUDIO_VER_MAJOR, BROOKESIA_SERVICE_AUDIO_VER_MINOR,
        BROOKESIA_SERVICE_AUDIO_VER_PATCH
    );

    auto registration = register_audio_encoder_dataflow_provider(*this, id_);
    if (!registration) {
        BROOKESIA_LOGE("Failed to register audio encoder DataFlow provider: %1%", registration.error());
        return false;
    }
    dataflow_provider_registration_.emplace(std::move(registration.value()));

    return true;
}


void AudioEncoder::on_deinit()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    dataflow_provider_registration_.reset();
}


AudioEncoder *AudioEncoder::get_instance(int id)
{
    std::lock_guard lock(get_encoder_registry_mutex());
    auto &registry = get_encoder_registry();
    if (id < 0 || static_cast<size_t>(id) >= registry.size()) {
        return nullptr;
    }
    return registry[static_cast<size_t>(id)];
}


bool AudioEncoder::on_start()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    auto encoder_handle = hal::acquire_interface<hal::audio::EncoderIface>(
                              hal::audio::EncoderIface::get_default_instance_name(id_)
                          );
    auto encoder_iface = encoder_handle.get();
    BROOKESIA_CHECK_NULL_RETURN(encoder_iface, false, "Failed to get audio encoder interface");
    encoder_iface_ = std::move(encoder_handle);

    return true;
}


void AudioEncoder::on_stop()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    ServiceManager::get_instance().get_dataflow_registry().invalidate_provider_operations(
        make_dataflow_provider_id("encoder", id_)
    );
    stop_encoder();
    encoder_iface_.reset();
}


std::expected<void, std::string> AudioEncoder::function_start(const boost::json::object &config)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_LOGD("Params: config(%1%)", BROOKESIA_DESCRIBE_TO_STR(config));

    AudioEncoderDynamicConfig encoder_config;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(config, encoder_config)) {
        return std::unexpected("Failed to parse encoder dynamic config");
    }
    if (encoder_config.type == BaseHelper::CodecFormat::PCM) {
        auto &general = encoder_config.general;
        auto target_fetch_data_size = general.frame_duration * general.sample_rate * general.channels *
                                      general.sample_bits / 8 / 1000 + ENCODER_FETCH_DATA_SIZE_MORE;
        if (encoder_config.fetch_data_size != target_fetch_data_size) {
            BROOKESIA_LOGW(
                "Detected different fetch data size for PCM type, adjusted from %1% to %2%",
                encoder_config.fetch_data_size, target_fetch_data_size
            );
            encoder_config.fetch_data_size = target_fetch_data_size;
        }
    }

    if (!start_encoder(encoder_config)) {
        return std::unexpected("Failed to start encoder");
    }

    return {};
}


std::expected<void, std::string> AudioEncoder::function_stop()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    stop_encoder();

    return {};
}


std::expected<void, std::string> AudioEncoder::function_pause()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (!encoder_iface_) {
        return std::unexpected("Audio encoder is not available");
    }
    encoder_iface_->pause();

    return {};
}


std::expected<void, std::string> AudioEncoder::function_resume()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (!encoder_iface_) {
        return std::unexpected("Audio encoder is not available");
    }
    encoder_iface_->resume();

    return {};
}


std::expected<void, std::string> AudioEncoder::function_pause_wake_end()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(encoder_state_mutex_);
    wake_end_paused_ = true;
    if (wake_end_task_id_ != 0) {
        auto scheduler = get_task_scheduler();
        if (scheduler) {
            scheduler->cancel(wake_end_task_id_);
        }
        wake_end_task_id_ = 0;
        wake_end_remaining_ms_ = std::max<int64_t>(1, wake_end_deadline_ms_ - get_current_time_ms());
        wake_end_deadline_ms_ = 0;
    }

    return {};
}


std::expected<void, std::string> AudioEncoder::function_resume_wake_end()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(encoder_state_mutex_);
    wake_end_paused_ = false;
    if (wake_end_pending_ && (wake_end_task_id_ == 0) && (wake_end_remaining_ms_ > 0)) {
        if (!schedule_wake_end_locked(static_cast<uint32_t>(wake_end_remaining_ms_))) {
            return std::unexpected("Failed to resume WakeEnd timer");
        }
    }

    return {};
}


std::expected<boost::json::array, std::string> AudioEncoder::function_get_afe_wake_words()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (!encoder_iface_) {
        return std::unexpected("Audio encoder is not available");
    }

    auto wake_words_array = encoder_iface_->get_afe_wake_words();
    return BROOKESIA_DESCRIBE_TO_JSON(wake_words_array).as_array();
}


bool AudioEncoder::start_encoder(const AudioEncoderDynamicConfig &config)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_CHECK_FALSE_RETURN(static_cast<bool>(encoder_iface_), false, "Audio encoder is not available");
    if (encoder_iface_->is_started()) {
        return true;
    }

    {
        std::lock_guard lock(encoder_state_mutex_);
        encoder_config_ = config;
        wake_end_paused_ = false;
        wake_end_pending_ = false;
        wake_end_task_id_ = 0;
        wake_end_deadline_ms_ = 0;
        wake_end_remaining_ms_ = 0;
        wake_end_session_id_++;
    }

    hal::audio::EncoderIface::Callbacks callbacks{
        .afe_event = [this](AudioAFE_Event event)
        {
            on_afe_event(event);
        },
        .recorder_data = [this](const uint8_t *data, size_t size)
        {
            on_recorder_input_data(data, size);
        },
    };
    BROOKESIA_CHECK_FALSE_RETURN(
        encoder_iface_->start(config, std::move(callbacks)), false, "Failed to start encoder"
    );
    if (!start_encoder_fetch_task(config)) {
        encoder_iface_->stop();
        return false;
    }

    BROOKESIA_LOGI("Encoder started");

    return true;
}


void AudioEncoder::stop_encoder()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    stop_encoder_fetch_task();
    cancel_wake_end();

    if (!encoder_iface_ || !encoder_iface_->is_started()) {
        return;
    }

    encoder_iface_->stop();
    BROOKESIA_LOGI("Encoder stopped");
}


bool AudioEncoder::start_encoder_fetch_task(const AudioEncoderDynamicConfig &config)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    auto scheduler = get_task_scheduler();
    BROOKESIA_CHECK_NULL_RETURN(scheduler, false, "Task scheduler is not available");

    {
        std::lock_guard lock(encoder_state_mutex_);
        encoder_fetch_buffer_.resize(config.fetch_data_size);
    }

    auto task = [this, fetch_data_size = config.fetch_data_size, fetch_interval_ms = config.fetch_interval_ms]() {
        return encoder_fetch_task(fetch_data_size, fetch_interval_ms);
    };
    auto result = scheduler->post_periodic(
                      task, static_cast<int>(std::max<uint32_t>(1, config.fetch_interval_ms)),
                      &encoder_fetch_task_id_, get_call_task_group()
                  );
    BROOKESIA_CHECK_FALSE_RETURN(result, false, "Failed to schedule encoder fetch task");

    return true;
}


void AudioEncoder::stop_encoder_fetch_task()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (encoder_fetch_task_id_ != 0) {
        auto scheduler = get_task_scheduler();
        if (scheduler) {
            scheduler->cancel(encoder_fetch_task_id_);
        }
        encoder_fetch_task_id_ = 0;
    }
    std::lock_guard lock(encoder_state_mutex_);
    encoder_fetch_buffer_.clear();
}


bool AudioEncoder::encoder_fetch_task(uint32_t fetch_data_size, uint32_t fetch_interval_ms)
{
    if (fetch_data_size == 0) {
        BROOKESIA_LOGE("Invalid encoder fetch data size");
        return false;
    }

    (void)fetch_interval_ms;

    // Snapshot the encoder interface under the state lock, then run the blocking read WITHOUT
    // holding encoder_state_mutex_. Holding the lock across read_encoded_data() deadlocks with the
    // AFE event path: on_afe_event()/schedule_wake_end() block acquiring encoder_state_mutex_ from
    // the AFE dispatch context, while this task waits inside read_encoded_data() for the AFE to
    // produce a frame, which it cannot until that dispatch completes -> AFE(FEED) ringbuffer overflow.
    std::shared_ptr<hal::audio::EncoderIface> iface;
    {
        std::lock_guard lock(encoder_state_mutex_);
        if (!encoder_iface_ || !encoder_iface_->is_started()) {
            return false;
        }
        iface = encoder_iface_.get();
    }

    std::vector<uint8_t> data(fetch_data_size);
    int ret_size = iface->read_encoded_data(data.data(), data.size());
    if (ret_size <= 0) {
        if (ret_size < 0) {
            BROOKESIA_LOGW("Failed to read encoded data");
        }
        return true;
    }
    data.resize(static_cast<size_t>(ret_size));

    RawBuffer buffer(data.data(), data.size());
    encoded_data_signal_(buffer);
    return true;
}


void AudioEncoder::on_afe_event(AudioAFE_Event afe_event)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (afe_event == BaseHelper::AFE_Event::Max) {
        return;
    }

    publish_afe_event(afe_event);
    AudioEncoderDynamicConfig encoder_config = {};
    {
        std::lock_guard lock(encoder_state_mutex_);
        encoder_config = encoder_config_;
    }

    switch (afe_event) {
    case BaseHelper::AFE_Event::WakeStart:
        schedule_wake_end(encoder_config.afe_wake_start_timeout_ms);
        break;
    case BaseHelper::AFE_Event::VAD_Start:
        cancel_wake_end();
        break;
    case BaseHelper::AFE_Event::VAD_End:
        schedule_wake_end(encoder_config.afe_wake_end_timeout_ms);
        break;
    case BaseHelper::AFE_Event::WakeEnd:
    case BaseHelper::AFE_Event::Max:
        break;
    }
}


void AudioEncoder::publish_afe_event(AudioAFE_Event afe_event)
{
    afe_event_signal_(afe_event);

    auto result = publish_event(BROOKESIA_DESCRIBE_TO_STR(Helper::EventId::AFEEventHappened), {
        BROOKESIA_DESCRIBE_TO_STR(afe_event)
    });
    BROOKESIA_CHECK_FALSE_EXIT(result, "Failed to publish afe event happened event");
}


void AudioEncoder::on_recorder_input_data(const uint8_t *data, size_t size)
{
    RawBuffer buffer(data, size);
    recorder_data_signal_(buffer);
}


void AudioEncoder::schedule_wake_end(uint32_t timeout_ms)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(encoder_state_mutex_);
    cancel_wake_end_locked();
    if (!encoder_config_.enable_afe || (timeout_ms == 0)) {
        return;
    }

    wake_end_pending_ = true;
    wake_end_remaining_ms_ = timeout_ms;
    if (wake_end_paused_) {
        return;
    }

    BROOKESIA_CHECK_FALSE_EXECUTE(schedule_wake_end_locked(timeout_ms), {}, {
        BROOKESIA_LOGE("Failed to schedule WakeEnd event");
    });
}


bool AudioEncoder::schedule_wake_end_locked(uint32_t timeout_ms)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    auto scheduler = get_task_scheduler();
    BROOKESIA_CHECK_NULL_RETURN(scheduler, false, "Task scheduler is not available");

    const uint32_t session_id = ++wake_end_session_id_;
    lib_utils::TaskScheduler::TaskId task_id = 0;
    auto result = scheduler->post_delayed([this, session_id]() {
        on_wake_end_timeout(session_id);
    }, static_cast<int>(timeout_ms), &task_id, get_call_task_group());
    BROOKESIA_CHECK_FALSE_RETURN(result, false, "Failed to post WakeEnd delayed task");

    wake_end_task_id_ = task_id;
    wake_end_deadline_ms_ = get_current_time_ms() + timeout_ms;
    wake_end_remaining_ms_ = timeout_ms;
    wake_end_pending_ = true;

    return true;
}


void AudioEncoder::cancel_wake_end()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::lock_guard lock(encoder_state_mutex_);
    cancel_wake_end_locked();
}


void AudioEncoder::cancel_wake_end_locked()
{
    if (wake_end_task_id_ != 0) {
        auto scheduler = get_task_scheduler();
        if (scheduler) {
            scheduler->cancel(wake_end_task_id_);
        }
    }
    wake_end_task_id_ = 0;
    wake_end_deadline_ms_ = 0;
    wake_end_remaining_ms_ = 0;
    wake_end_pending_ = false;
    wake_end_session_id_++;
}


void AudioEncoder::on_wake_end_timeout(uint32_t session_id)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    {
        std::lock_guard lock(encoder_state_mutex_);
        if ((session_id != wake_end_session_id_) || wake_end_paused_ || !wake_end_pending_) {
            return;
        }
        wake_end_task_id_ = 0;
        wake_end_deadline_ms_ = 0;
        wake_end_remaining_ms_ = 0;
        wake_end_pending_ = false;
    }

    publish_afe_event(BaseHelper::AFE_Event::WakeEnd);
}


}
