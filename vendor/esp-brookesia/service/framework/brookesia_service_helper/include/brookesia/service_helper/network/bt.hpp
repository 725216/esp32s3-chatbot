/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bt.hpp
 * @brief Defines the helper schema for the generic Bluetooth service.
 */
#pragma once

#include <array>
#include <optional>
#include <span>
#include <vector>

#include "brookesia/hal_interface/interfaces/bluetooth/types.hpp"
#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_manager/detail/static_schema.hpp"
#include "brookesia/service_manager/helper/base.hpp"

namespace esp_brookesia::service::helper {

/** Helper contract for Bluetooth host and profile control. */
class Bt: public Base<Bt> {
public:
    using Profile = hal::bluetooth::Profile;
    using HostState = hal::bluetooth::HostState;
    using ConnectionState = hal::bluetooth::ConnectionState;
    using StreamState = hal::bluetooth::StreamState;
    using PlaybackStatus = hal::bluetooth::PlaybackStatus;
    using DeviceConfig = hal::bluetooth::DeviceConfig;
    using PeerInfo = hal::bluetooth::PeerInfo;
    using TrackMetadata = hal::bluetooth::TrackMetadata;

    struct Capabilities {
        std::vector<Profile> profiles;
        bool classic_supported = false;
        bool ble_supported = false;
    };

    struct State {
        HostState host_state = HostState::Idle;
        bool is_supported = false;
        bool is_started = false;
        std::vector<Profile> profiles;
    };

    struct A2dpState {
        ConnectionState connection_state = ConnectionState::Disconnected;
        StreamState stream_state = StreamState::Idle;
        PlaybackStatus playback_status = PlaybackStatus::Unknown;
        uint8_t volume = 0;
        std::optional<PeerInfo> connection;
        TrackMetadata metadata;
    };

    enum class FunctionId {
        GetCapabilities,
        SetDeviceConfig,
        GetDeviceConfig,
        Start,
        Stop,
        GetState,
        ListProfiles,
        GetConnections,
        Disconnect,
        A2dpSinkStart,
        A2dpSinkStop,
        A2dpPause,
        A2dpResume,
        A2dpNext,
        A2dpPrevious,
        A2dpSetVolume,
        A2dpGetVolume,
        Max,
    };

    enum class EventId {
        StateChanged,
        ProfileAvailabilityChanged,
        ConnectionStateChanged,
        A2dpStreamStateChanged,
        PlaybackStatusChanged,
        MetadataChanged,
        VolumeChanged,
        ErrorHappened,
        Max,
    };

private:
    using FunctionParameterSpec = detail::static_schema::FunctionParameterSpec;
    using FunctionReturnSpec = detail::static_schema::FunctionReturnSpec;
    using FunctionSpec = detail::static_schema::FunctionSpec;
    using EventItemSpec = detail::static_schema::EventItemSpec;
    using EventSpec = detail::static_schema::EventSpec;

    inline static constexpr std::array<FunctionParameterSpec, 1> SET_DEVICE_CONFIG_PARAMETERS = {{
            {.name = "Config", .description = "Bluetooth device configuration.", .type = FunctionValueType::Object},
        }
    };
    inline static constexpr std::array<FunctionParameterSpec, 1> DISCONNECT_PARAMETERS = {{
            {
                .name = "ConnectionId",
                .description = "Bluetooth connection identifier.",
                .type = FunctionValueType::Number,
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
                .name = "GetCapabilities",
                .description = "Get available Bluetooth host and profile capabilities.",
                .parameters = {},
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Object,
                    .description = "Bluetooth host and profile capabilities object.",
                },
            },
            {
                .name = "SetDeviceConfig",
                .description = "Set Bluetooth device configuration.",
                .parameters = SET_DEVICE_CONFIG_PARAMETERS,
            },
            {
                .name = "GetDeviceConfig",
                .description = "Get Bluetooth device configuration.",
                .parameters = {},
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Object,
                    .description = "Bluetooth device configuration object.",
                },
            },
            {.name = "Start", .description = "Start the Bluetooth host.", .parameters = {}},
            {.name = "Stop", .description = "Stop the Bluetooth host.", .parameters = {}},
            {
                .name = "GetState",
                .description = "Get Bluetooth host state.",
                .parameters = {},
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Object,
                    .description = "Bluetooth host state object.",
                },
            },
            {
                .name = "ListProfiles",
                .description = "List currently available Bluetooth profiles.",
                .parameters = {},
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Array,
                    .description = "Available Bluetooth profile list.",
                },
            },
            {
                .name = "GetConnections",
                .description = "Get Bluetooth profile connections.",
                .parameters = {},
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Array,
                    .description = "Current Bluetooth connection list.",
                },
            },
            {.name = "Disconnect", .description = "Disconnect a Bluetooth peer.", .parameters = DISCONNECT_PARAMETERS},
            {.name = "A2dpSinkStart", .description = "Start the A2DP Sink profile.", .parameters = {}},
            {.name = "A2dpSinkStop", .description = "Stop the A2DP Sink profile.", .parameters = {}},
            {.name = "A2dpPause", .description = "Pause remote A2DP playback.", .parameters = {}},
            {.name = "A2dpResume", .description = "Resume remote A2DP playback.", .parameters = {}},
            {.name = "A2dpNext", .description = "Skip to the next remote track.", .parameters = {}},
            {.name = "A2dpPrevious", .description = "Skip to the previous remote track.", .parameters = {}},
            {.name = "A2dpSetVolume", .description = "Set A2DP absolute volume.", .parameters = SET_VOLUME_PARAMETERS},
            {
                .name = "A2dpGetVolume",
                .description = "Get A2DP absolute volume.",
                .parameters = {},
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Number,
                    .description = "A2DP absolute volume percentage in the range 0..100.",
                },
            },
        }
    };
    static_assert(FUNCTION_SPECS.size() == static_cast<size_t>(FunctionId::Max));

    inline static constexpr std::array<EventItemSpec, 1> STATE_CHANGED_ITEMS = {{
            {.name = "State", .description = "Bluetooth host state object.", .type = EventItemType::Object},
        }
    };
    inline static constexpr std::array<EventItemSpec, 2> PROFILE_AVAILABILITY_CHANGED_ITEMS = {{
            {.name = "Profile", .description = "Bluetooth profile identifier.", .type = EventItemType::String},
            {.name = "Available", .description = "Whether the profile is available.", .type = EventItemType::Boolean},
        }
    };
    inline static constexpr std::array<EventItemSpec, 2> CONNECTION_STATE_CHANGED_ITEMS = {{
            {.name = "State", .description = "Connection state.", .type = EventItemType::String},
            {.name = "Connection", .description = "Peer information object.", .type = EventItemType::Object},
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
            {.name = "Metadata", .description = "Current track metadata object.", .type = EventItemType::Object},
        }
    };
    inline static constexpr std::array<EventItemSpec, 1> VOLUME_CHANGED_ITEMS = {{
            {.name = "Volume", .description = "Absolute volume percentage.", .type = EventItemType::Number},
        }
    };
    inline static constexpr std::array<EventItemSpec, 3> ERROR_HAPPENED_ITEMS = {{
            {.name = "Operation", .description = "Failed operation name.", .type = EventItemType::String},
            {.name = "Code", .description = "Backend error code.", .type = EventItemType::Number},
            {.name = "Message", .description = "Diagnostic error message.", .type = EventItemType::String},
        }
    };

    inline static constexpr std::array<EventSpec, static_cast<size_t>(EventId::Max)> EVENT_SPECS = {{
            {.name = "StateChanged", .description = "Bluetooth host state changed.", .items = STATE_CHANGED_ITEMS},
            {
                .name = "ProfileAvailabilityChanged",
                .description = "Bluetooth profile availability changed.",
                .items = PROFILE_AVAILABILITY_CHANGED_ITEMS,
            },
            {
                .name = "ConnectionStateChanged",
                .description = "Bluetooth connection state changed.",
                .items = CONNECTION_STATE_CHANGED_ITEMS,
            },
            {
                .name = "A2dpStreamStateChanged",
                .description = "A2DP stream state changed.",
                .items = STREAM_STATE_CHANGED_ITEMS,
            },
            {
                .name = "PlaybackStatusChanged",
                .description = "AVRCP playback status changed.",
                .items = PLAYBACK_STATUS_CHANGED_ITEMS,
            },
            {.name = "MetadataChanged", .description = "A2DP track metadata changed.", .items = METADATA_CHANGED_ITEMS},
            {.name = "VolumeChanged", .description = "A2DP volume changed.", .items = VOLUME_CHANGED_ITEMS},
            {
                .name = "ErrorHappened",
                .description = "A Bluetooth backend error occurred.",
                .items = ERROR_HAPPENED_ITEMS,
            },
        }
    };
    static_assert(EVENT_SPECS.size() == static_cast<size_t>(EventId::Max));

public:
    static constexpr std::string_view get_name()
    {
        return "Bt";
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

BROOKESIA_DESCRIBE_STRUCT(Bt::Capabilities, (), (profiles, classic_supported, ble_supported));
BROOKESIA_DESCRIBE_STRUCT(Bt::State, (), (host_state, is_supported, is_started, profiles));
BROOKESIA_DESCRIBE_STRUCT(
    Bt::A2dpState, (), (connection_state, stream_state, playback_status, volume, connection, metadata)
);
BROOKESIA_DESCRIBE_ENUM(Bt::Profile, BleGattPeripheral, A2dpSink, AvrcpController, Max);
BROOKESIA_DESCRIBE_ENUM(Bt::HostState, Idle, Starting, Started, Stopping, Error, Max);
BROOKESIA_DESCRIBE_ENUM(Bt::ConnectionState, Disconnected, Connecting, Connected, Disconnecting, Max);
BROOKESIA_DESCRIBE_ENUM(Bt::StreamState, Idle, Starting, Started, Stopping, Stopped, Error, Max);
BROOKESIA_DESCRIBE_ENUM(Bt::PlaybackStatus, Unknown, Stopped, Playing, Paused, Max);
BROOKESIA_DESCRIBE_ENUM(
    Bt::FunctionId,
    GetCapabilities, SetDeviceConfig, GetDeviceConfig, Start, Stop, GetState, ListProfiles, GetConnections,
    Disconnect, A2dpSinkStart, A2dpSinkStop, A2dpPause, A2dpResume, A2dpNext, A2dpPrevious, A2dpSetVolume,
    A2dpGetVolume, Max
);
BROOKESIA_DESCRIBE_ENUM(
    Bt::EventId,
    StateChanged, ProfileAvailabilityChanged, ConnectionStateChanged, A2dpStreamStateChanged,
    PlaybackStatusChanged, MetadataChanged, VolumeChanged, ErrorHappened, Max
);

} // namespace esp_brookesia::service::helper
