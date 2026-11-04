/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/hal_interface/interfaces/bluetooth/simulated_a2dp_sink.hpp"

#include <utility>

namespace esp_brookesia::hal::bluetooth {

bool SimulatedA2dpSinkBackend::is_supported() const
{
    std::lock_guard lock(mutex_);
    return supported_;
}

bool SimulatedA2dpSinkBackend::configure(const Config &config, Callbacks callbacks)
{
    std::lock_guard lock(mutex_);
    if (initialized_ || started_) {
        return false;
    }
    config_ = config;
    callbacks_ = std::move(callbacks);
    return true;
}

void SimulatedA2dpSinkBackend::clear_callbacks()
{
    std::lock_guard lock(mutex_);
    callbacks_ = {};
}

bool SimulatedA2dpSinkBackend::init()
{
    std::lock_guard lock(mutex_);
    initialized_ = true;
    return true;
}

void SimulatedA2dpSinkBackend::deinit()
{
    std::lock_guard lock(mutex_);
    started_ = false;
    initialized_ = false;
    connection_.reset();
}

bool SimulatedA2dpSinkBackend::start()
{
    std::lock_guard lock(mutex_);
    if (!initialized_ || !supported_) {
        return false;
    }
    started_ = true;
    return true;
}

void SimulatedA2dpSinkBackend::stop()
{
    std::lock_guard lock(mutex_);
    started_ = false;
}

bool SimulatedA2dpSinkBackend::pause()
{
    return simulate_playback_status(PlaybackStatus::Paused);
}

bool SimulatedA2dpSinkBackend::resume()
{
    return simulate_playback_status(PlaybackStatus::Playing);
}

bool SimulatedA2dpSinkBackend::next()
{
    return is_supported() && static_cast<bool>(get_connection());
}

bool SimulatedA2dpSinkBackend::previous()
{
    return is_supported() && static_cast<bool>(get_connection());
}

bool SimulatedA2dpSinkBackend::set_volume(uint8_t volume)
{
    return simulate_volume(volume);
}

uint8_t SimulatedA2dpSinkBackend::get_volume() const
{
    std::lock_guard lock(mutex_);
    return volume_;
}

std::optional<PeerInfo> SimulatedA2dpSinkBackend::get_connection() const
{
    std::lock_guard lock(mutex_);
    return connection_;
}

bool SimulatedA2dpSinkBackend::disconnect()
{
    return simulate_disconnect();
}

void SimulatedA2dpSinkBackend::set_supported(bool supported)
{
    std::lock_guard lock(mutex_);
    supported_ = supported;
}

bool SimulatedA2dpSinkBackend::simulate_connect(PeerInfo peer)
{
    Callbacks callbacks;
    {
        std::lock_guard lock(mutex_);
        if (!started_ || !supported_ || connection_) {
            return false;
        }
        connection_ = peer;
        callbacks = callbacks_;
    }
    if (callbacks.on_connection_changed) {
        callbacks.on_connection_changed(peer, ConnectionState::Connected);
    }
    return true;
}

bool SimulatedA2dpSinkBackend::simulate_disconnect()
{
    Callbacks callbacks;
    PeerInfo peer;
    {
        std::lock_guard lock(mutex_);
        if (!connection_) {
            return false;
        }
        peer = *connection_;
        connection_.reset();
        callbacks = callbacks_;
    }
    if (callbacks.on_connection_changed) {
        callbacks.on_connection_changed(peer, ConnectionState::Disconnected);
    }
    return true;
}

bool SimulatedA2dpSinkBackend::simulate_stream(StreamState state, PcmFrame frame)
{
    Callbacks callbacks;
    {
        std::lock_guard lock(mutex_);
        if (!started_ || !connection_) {
            return false;
        }
        callbacks = callbacks_;
    }
    if (callbacks.on_stream_changed) {
        callbacks.on_stream_changed(state);
    }
    if (state == StreamState::Started && callbacks.on_pcm && !frame.data.empty()) {
        callbacks.on_pcm(std::move(frame));
    }
    return true;
}

bool SimulatedA2dpSinkBackend::simulate_playback_status(PlaybackStatus status)
{
    Callbacks callbacks;
    {
        std::lock_guard lock(mutex_);
        if (!started_ || !connection_) {
            return false;
        }
        callbacks = callbacks_;
    }
    if (callbacks.on_playback_status_changed) {
        callbacks.on_playback_status_changed(status);
    }
    return true;
}

bool SimulatedA2dpSinkBackend::simulate_metadata(TrackMetadata metadata)
{
    Callbacks callbacks;
    {
        std::lock_guard lock(mutex_);
        if (!started_ || !connection_) {
            return false;
        }
        callbacks = callbacks_;
    }
    if (callbacks.on_metadata_changed) {
        callbacks.on_metadata_changed(metadata);
    }
    return true;
}

bool SimulatedA2dpSinkBackend::simulate_volume(uint8_t volume)
{
    Callbacks callbacks;
    {
        std::lock_guard lock(mutex_);
        if (!started_) {
            return false;
        }
        volume_ = volume;
        callbacks = callbacks_;
    }
    if (callbacks.on_volume_changed) {
        callbacks.on_volume_changed(volume);
    }
    return true;
}

} // namespace esp_brookesia::hal::bluetooth
