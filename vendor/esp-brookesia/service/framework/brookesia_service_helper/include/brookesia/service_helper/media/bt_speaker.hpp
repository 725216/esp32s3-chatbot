/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bt_speaker.hpp
 * @brief Defines the helper schema for the Bluetooth speaker service.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_helper/network/bt.hpp"
#include "brookesia/service_manager/detail/static_schema.hpp"
#include "brookesia/service_manager/helper/base.hpp"

namespace esp_brookesia::service::helper {

/** Helper contract for high-level Bluetooth speaker control and state. */
class BtSpeaker: public Base<BtSpeaker> {
public:
    using DeviceConfig = Bt::DeviceConfig;
    using ConnectionState = Bt::ConnectionState;
    using StreamState = Bt::StreamState;
    using PlaybackStatus = Bt::PlaybackStatus;
    using PeerInfo = Bt::PeerInfo;
    using TrackMetadata = Bt::TrackMetadata;

    enum class GeneralState {
        Idle,
        Ready,
        Starting,
        Started,
        Stopping,
        Error,
        Max,
    };

    struct Config {
        DeviceConfig device;
        bool stop_local_playback_on_connect = true;

        bool operator==(const Config &) const = default;
    };

    struct State {
        GeneralState general_state = GeneralState::Idle;
        bool is_configured = false;
        bool is_supported = false;
        bool is_started = false;
        bool is_connected = false;
        bool is_music_active = false;
        ConnectionState connection_state = ConnectionState::Disconnected;
        StreamState stream_state = StreamState::Idle;
        PlaybackStatus playback_status = PlaybackStatus::Unknown;
        uint8_t volume = 0;
        std::optional<PeerInfo> connection;
        TrackMetadata metadata;
    };

    enum class FunctionId {
        SetConfig,
        GetConfig,
        Start,
        Stop,
        GetState,
        Pause,
        Resume,
        Next,
        Previous,
        SetVolume,
        GetVolume,
        Disconnect,
        Max,
    };

    enum class EventId {
        StateChanged,
        ConnectionStateChanged,
        StreamStateChanged,
        PlaybackStatusChanged,
        MetadataChanged,
        VolumeChanged,
        ErrorHappened,
        Max,
    };

private:
    using EventItemSpec = detail::static_schema::EventItemSpec;
    using EventSpec = detail::static_schema::EventSpec;
    using FunctionParameterSpec = detail::static_schema::FunctionParameterSpec;
    using FunctionReturnSpec = detail::static_schema::FunctionReturnSpec;
    using FunctionSpec = detail::static_schema::FunctionSpec;

    inline static constexpr std::array<FunctionParameterSpec, 1> SET_CONFIG_PARAMETERS = {{
            {
                .name = "Config",
                .description = "Bluetooth speaker configuration.",
                .type = FunctionValueType::Object,
            },
        }
    };
    inline static constexpr std::array<FunctionParameterSpec, 1> SET_VOLUME_PARAMETERS = {{
            {
                .name = "Volume",
                .description = "Absolute volume percentage in the range 0..100.",
                .type = FunctionValueType::Number,
            },
        }
    };

    inline static constexpr std::array<FunctionSpec, static_cast<size_t>(FunctionId::Max)> FUNCTION_SPECS = {{
            {
                .name = "SetConfig",
                .description = "Set Bluetooth speaker configuration before starting.",
                .parameters = SET_CONFIG_PARAMETERS,
            },
            {
                .name = "GetConfig",
                .description = "Get Bluetooth speaker configuration.",
                .parameters = {},
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Object,
                    .description = "Bluetooth speaker configuration object.",
                },
            },
            {.name = "Start", .description = "Start the Bluetooth speaker profile.", .parameters = {}},
            {.name = "Stop", .description = "Stop the Bluetooth speaker profile.", .parameters = {}},
            {
                .name = "GetState",
                .description = "Get the complete Bluetooth speaker state.",
                .parameters = {},
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Object,
                    .description = "Bluetooth speaker state object.",
                },
            },
            {.name = "Pause", .description = "Pause remote playback.", .parameters = {}},
            {.name = "Resume", .description = "Resume remote playback.", .parameters = {}},
            {.name = "Next", .description = "Skip to the next remote track.", .parameters = {}},
            {.name = "Previous", .description = "Skip to the previous remote track.", .parameters = {}},
            {
                .name = "SetVolume",
                .description = "Set the remote absolute volume.",
                .parameters = SET_VOLUME_PARAMETERS,
            },
            {
                .name = "GetVolume",
                .description = "Get the remote absolute volume.",
                .parameters = {},
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Number,
                    .description = "Absolute volume percentage in the range 0..100.",
                },
            },
            {.name = "Disconnect", .description = "Disconnect the current Bluetooth peer.", .parameters = {}},
        }
    };
    static_assert(FUNCTION_SPECS.size() == static_cast<size_t>(FunctionId::Max));

    inline static constexpr std::array<EventItemSpec, 1> STATE_CHANGED_ITEMS = {{
            {.name = "State", .description = "Complete Bluetooth speaker state.", .type = EventItemType::Object},
        }
    };
    inline static constexpr std::array<EventItemSpec, 2> CONNECTION_STATE_CHANGED_ITEMS = {{
            {.name = "State", .description = "Bluetooth connection state.", .type = EventItemType::String},
            {.name = "Connection", .description = "Connected peer information.", .type = EventItemType::Object},
        }
    };
    inline static constexpr std::array<EventItemSpec, 1> STREAM_STATE_CHANGED_ITEMS = {{
            {.name = "State", .description = "A2DP stream state.", .type = EventItemType::String},
        }
    };
    inline static constexpr std::array<EventItemSpec, 1> PLAYBACK_STATUS_CHANGED_ITEMS = {{
            {.name = "Status", .description = "AVRCP playback status.", .type = EventItemType::String},
        }
    };
    inline static constexpr std::array<EventItemSpec, 1> METADATA_CHANGED_ITEMS = {{
            {.name = "Metadata", .description = "Current track metadata.", .type = EventItemType::Object},
        }
    };
    inline static constexpr std::array<EventItemSpec, 1> VOLUME_CHANGED_ITEMS = {{
            {.name = "Volume", .description = "Absolute volume percentage.", .type = EventItemType::Number},
        }
    };
    inline static constexpr std::array<EventItemSpec, 3> ERROR_HAPPENED_ITEMS = {{
            {.name = "Operation", .description = "Failed operation name.", .type = EventItemType::String},
            {.name = "Code", .description = "Backend-independent error code.", .type = EventItemType::Number},
            {.name = "Message", .description = "Diagnostic error message.", .type = EventItemType::String},
        }
    };

    inline static constexpr std::array<EventSpec, static_cast<size_t>(EventId::Max)> EVENT_SPECS = {{
            {
                .name = "StateChanged",
                .description = "Bluetooth speaker state changed.",
                .items = STATE_CHANGED_ITEMS,
            },
            {
                .name = "ConnectionStateChanged",
                .description = "The Bluetooth peer connection state changed.",
                .items = CONNECTION_STATE_CHANGED_ITEMS,
            },
            {
                .name = "StreamStateChanged",
                .description = "The A2DP stream state changed.",
                .items = STREAM_STATE_CHANGED_ITEMS,
            },
            {
                .name = "PlaybackStatusChanged",
                .description = "The AVRCP playback status changed.",
                .items = PLAYBACK_STATUS_CHANGED_ITEMS,
            },
            {
                .name = "MetadataChanged",
                .description = "The current track metadata changed.",
                .items = METADATA_CHANGED_ITEMS,
            },
            {
                .name = "VolumeChanged",
                .description = "The A2DP absolute volume changed.",
                .items = VOLUME_CHANGED_ITEMS,
            },
            {
                .name = "ErrorHappened",
                .description = "A Bluetooth speaker operation failed.",
                .items = ERROR_HAPPENED_ITEMS,
            },
        }
    };
    static_assert(EVENT_SPECS.size() == static_cast<size_t>(EventId::Max));

public:
    static constexpr std::string_view get_name()
    {
        return "BtSpeaker";
    }

    static std::span<const FunctionSchema> get_function_schemas()
    {
        static std::array<FunctionSchema, FUNCTION_SPECS.size()> schemas;
        static const bool initialized = [] {
            detail::static_schema::materialize_function_schemas(FUNCTION_SPECS, schemas);
            return true;
        }();
        static_cast<void>(initialized);
        return schemas;
    }

    static std::span<const EventSchema> get_event_schemas()
    {
        static std::array<EventSchema, EVENT_SPECS.size()> schemas;
        static const bool initialized = [] {
            detail::static_schema::materialize_event_schemas(EVENT_SPECS, schemas);
            return true;
        }();
        static_cast<void>(initialized);
        return schemas;
    }
};

BROOKESIA_DESCRIBE_ENUM(BtSpeaker::GeneralState, Idle, Ready, Starting, Started, Stopping, Error, Max);
BROOKESIA_DESCRIBE_STRUCT(BtSpeaker::Config, (), (device, stop_local_playback_on_connect));
BROOKESIA_DESCRIBE_STRUCT(
    BtSpeaker::State, (),
    (general_state, is_configured, is_supported, is_started, is_connected, is_music_active, connection_state,
     stream_state, playback_status, volume, connection, metadata)
);
BROOKESIA_DESCRIBE_ENUM(
    BtSpeaker::FunctionId,
    SetConfig, GetConfig, Start, Stop, GetState, Pause, Resume, Next, Previous, SetVolume, GetVolume, Disconnect, Max
);
BROOKESIA_DESCRIBE_ENUM(
    BtSpeaker::EventId,
    StateChanged, ConnectionStateChanged, StreamStateChanged, PlaybackStatusChanged, MetadataChanged, VolumeChanged,
    ErrorHappened, Max
);

} // namespace esp_brookesia::service::helper
