/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/hal_interface/interfaces/video/processor.hpp"
#include "brookesia/service_manager/detail/static_schema.hpp"
#include "brookesia/service_manager/helper/base.hpp"

namespace esp_brookesia::service::helper {

/**
 * @brief Shared schema/type definitions for video encoder and decoder helper services.
 */
class Video {
public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////// The following are the service specific types and enumerations ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    using EncoderSinkFormat = hal::video::EncoderSinkFormat;
    using EncoderSinkInfo = hal::video::EncoderSinkInfo;
    using EncoderSourceConfig = hal::video::EncoderSourceConfig;
    using DecoderSourceFormat = hal::video::DecoderSourceFormat;
    using DecoderSinkFormat = hal::video::DecoderSinkFormat;

    struct EncoderDisplayConfig {
        std::string output_name;
        std::string source_name;
        std::string source_role;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t draw_timeout_ms = 0;
        bool publish_sink_event = false;
        bool activate_source = true;
        uint32_t sink_index = 0;
    };

    struct EncoderConfig {
        std::vector<EncoderSinkInfo> sinks;
        bool enable_stream_mode = false;
        std::optional<EncoderSourceConfig> source = std::nullopt;
        std::optional<EncoderDisplayConfig> display = std::nullopt;
    };

    struct DecoderDisplayConfig {
        std::string output_name;
        std::string source_name;
        std::string source_role;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t draw_timeout_ms = 0;
        bool publish_sink_event = false;
    };

    struct DecoderConfig {
        uint16_t width = 0;
        uint16_t height = 0;
        DecoderSourceFormat source_format = DecoderSourceFormat::Max;
        DecoderSinkFormat sink_format = DecoderSinkFormat::Max;
        bool enable_stream_mode = false;
        bool enable_hw_acceleration = false;
        std::optional<DecoderDisplayConfig> display = std::nullopt;
    };

    /**
     * @brief Prefix used to build encoder helper service names.
     */
    static constexpr std::string_view ENCODER_NAME_PREFIX = "VideoEncoder";
    /**
     * @brief Prefix used to build decoder helper service names.
     */
    static constexpr std::string_view DECODER_NAME_PREFIX = "VideoDecoder";
    static constexpr std::string_view DISPLAY_SOURCE_NAME = "Video";
    static constexpr std::string_view DISPLAY_SOURCE_ROLE = "video";

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the types required by the Base class /////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Video encoder function identifiers.
     */
    enum class EncoderFunctionId : uint8_t {
        Open,
        Close,
        Start,
        Stop,
        FetchFrame,
        Max,
    };
    /**
     * @brief Video encoder event identifiers.
     */
    enum class EncoderEventId : uint8_t {
        StreamSinkFrameReady,
        FetchSinkFrameReady,
        Max,
    };

    /**
     * @brief Video decoder function identifiers.
     */
    enum class DecoderFunctionId : uint8_t {
        Open,
        Close,
        Start,
        Stop,
        FeedFrame,
        Max,
    };
    /**
     * @brief Video decoder event identifiers.
     */
    enum class DecoderEventId : uint8_t {
        SinkFrameReady,
        Max,
    };

    /**
     * @brief Parameter keys for `EncoderFunctionId::Open`.
     */
    /**
     * @brief Parameter keys for `EncoderFunctionId::FetchFrame`.
     */

    /**
     * @brief Parameter keys for `DecoderFunctionId::Open`.
     */
    /**
     * @brief Parameter keys for `DecoderFunctionId::FeedFrame`.
     */

    /**
     * @brief Item keys for `EncoderEventId::StreamSinkFrameReady`.
     */
    /**
     * @brief Item keys for `EncoderEventId::FetchSinkFrameReady`.
     */
    /**
     * @brief Item keys for `DecoderEventId::SinkFrameReady`.
     */

private:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the function schemas /////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    using DefaultValueKind = detail::static_schema::DefaultValueKind;
    using DefaultValueSpec = detail::static_schema::DefaultValueSpec;
    using EventItemSpec = detail::static_schema::EventItemSpec;
    using EventSpec = detail::static_schema::EventSpec;
    using FunctionParameterSpec = detail::static_schema::FunctionParameterSpec;
    using FunctionSpec = detail::static_schema::FunctionSpec;

    inline static constexpr DefaultValueSpec ZERO_NUMBER_DEFAULT = {
        .kind = DefaultValueKind::Number,
        .number = 0,
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> ENCODER_OPEN_PARAMETERS = {{
            {
                .name = "Config",
                .description =
                R"(Encoder config. Example: {"sinks":[{"format":"H264","width":320,"height":240,"fps":30},)"
                R"({"format":"MJPEG","width":320,"height":240,"fps":15}],"enable_stream_mode":true,)"
                R"("source":{"device_path":"/dev/video0","fixed_format":"RGB565","fixed_width":320,)"
                R"("fixed_height":240,"v4l2_buffer_count":2},"display":{"output_name":"Output0",)"
                R"("source_name":"Video","source_role":"video","x":0,"y":0,"draw_timeout_ms":1000,)"
                R"("publish_sink_event":false,"activate_source":true,"sink_index":0}})",
                .type = FunctionValueType::Object,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> ENCODER_FETCH_FRAME_PARAMETERS = {{
            {
                .name = "SinkIndex",
                .description = "Sink index.",
                .type = FunctionValueType::Number,
                .default_value = ZERO_NUMBER_DEFAULT,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> DECODER_OPEN_PARAMETERS = {{
            {
                .name = "Config",
                .description =
                R"(Decoder config. Example: {"width":0,"height":0,"source_format":"MJPEG","sink_format":"Max",)"
                R"("enable_stream_mode":true,"enable_hw_acceleration":true,"display":{"output_name":"Output0",)"
                R"("source_name":"Video","source_role":"video","x":0,"y":0,"draw_timeout_ms":1000,)"
                R"("publish_sink_event":false}})",
                .type = FunctionValueType::Object,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> DECODER_FEED_FRAME_PARAMETERS = {{
            {
                .name = "Frame",
                .description = "Frame data.",
                .type = FunctionValueType::RawBuffer,
            },
        }
    };

    inline static constexpr std::array<FunctionSpec, static_cast<size_t>(EncoderFunctionId::Max)>
    ENCODER_FUNCTION_SPECS = {{
            {
                .name = "Open",
                .description =
                "Open the encoder with config. If `display` is set, sink format is derived from the "
                "selected Display output when `Max`; width/height are filled from the output area when zero. "
                "When stream mode is enabled, frames are emitted automatically through `StreamSinkFrameReady`; "
                "`FetchFrame` is only for non-stream mode.",
                .parameters = ENCODER_OPEN_PARAMETERS,
                .default_timeout_ms = 2000,
            },
            {
                .name = "Close",
                .description = "Close encoder.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .require_scheduler = false,
            },
            {
                .name = "Start",
                .description = "Start encoder.",
                .parameters = std::span<const FunctionParameterSpec>{},
            },
            {
                .name = "Stop",
                .description = "Stop encoder.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .require_scheduler = false,
            },
            {
                .name = "FetchFrame",
                .description = "Fetch an encoder output frame and emit `FetchSinkFrameReady`. "
                "Only available in non-stream mode; stream mode emits `StreamSinkFrameReady` "
                "automatically.",
                .parameters = ENCODER_FETCH_FRAME_PARAMETERS,
            },
        }
    };
    static_assert(ENCODER_FUNCTION_SPECS.size() == static_cast<size_t>(EncoderFunctionId::Max));

    inline static constexpr std::array<FunctionSpec, static_cast<size_t>(DecoderFunctionId::Max)>
    DECODER_FUNCTION_SPECS = {{
            {
                .name = "Open",
                .description =
                "Open the decoder with config. If `display` is set, sink format is derived from the "
                "selected Display output; width/height are used when non-zero, otherwise filled from the output "
                "area.",
                .parameters = DECODER_OPEN_PARAMETERS,
            },
            {
                .name = "Close",
                .description = "Close decoder.",
                .parameters = std::span<const FunctionParameterSpec>{},
            },
            {
                .name = "Start",
                .description = "Start decoder.",
                .parameters = std::span<const FunctionParameterSpec>{},
            },
            {
                .name = "Stop",
                .description = "Stop decoder.",
                .parameters = std::span<const FunctionParameterSpec>{},
            },
            {
                .name = "FeedFrame",
                .description = "Feed a decoder input frame.",
                .parameters = DECODER_FEED_FRAME_PARAMETERS,
            },
        }
    };
    static_assert(DECODER_FUNCTION_SPECS.size() == static_cast<size_t>(DecoderFunctionId::Max));

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the event schemas /////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    inline static constexpr std::array<EventItemSpec, 3> ENCODER_STREAM_SINK_FRAME_READY_ITEMS = {{
            {"SinkIndex", "Sink index.", EventItemType::Number},
            {
                "SinkInfo", R"(Sink info. Example: {"format":"H264","width":320,"height":240,"fps":30})",
                EventItemType::Object
            },
            {"Frame", "Encoded frame data.", EventItemType::RawBuffer},
        }
    };

    inline static constexpr std::array<EventItemSpec, 3> ENCODER_FETCH_SINK_FRAME_READY_ITEMS = {{
            {"SinkIndex", "Sink index.", EventItemType::Number},
            {
                "SinkInfo", R"(Sink info. Example: {"format":"MJPEG","width":320,"height":240,"fps":15})",
                EventItemType::Object
            },
            {"Frame", "Encoded frame data.", EventItemType::RawBuffer},
        }
    };

    inline static constexpr std::array<EventItemSpec, 3> DECODER_SINK_FRAME_READY_ITEMS = {{
            {"Width", "Decoded frame width.", EventItemType::Number},
            {"Height", "Decoded frame height.", EventItemType::Number},
            {"Frame", "Decoded frame data.", EventItemType::RawBuffer},
        }
    };

    inline static constexpr std::array<EventSpec, static_cast<size_t>(EncoderEventId::Max)> ENCODER_EVENT_SPECS = {{
            {
                .name = "StreamSinkFrameReady",
                .description = "Emitted when an encoder stream frame is ready. Stream mode only.",
                .items = ENCODER_STREAM_SINK_FRAME_READY_ITEMS,
                .require_scheduler = false,
            },
            {
                .name = "FetchSinkFrameReady",
                .description = "Emitted when an encoder fetched frame is ready. Non-stream mode only.",
                .items = ENCODER_FETCH_SINK_FRAME_READY_ITEMS,
                .require_scheduler = false,
            },
        }
    };
    static_assert(ENCODER_EVENT_SPECS.size() == static_cast<size_t>(EncoderEventId::Max));

    inline static constexpr std::array<EventSpec, static_cast<size_t>(DecoderEventId::Max)> DECODER_EVENT_SPECS = {{
            {
                .name = "SinkFrameReady",
                .description = "Emitted when a decoder output frame is ready.",
                .items = DECODER_SINK_FRAME_READY_ITEMS,
                .require_scheduler = false,
            },
        }
    };
    static_assert(DECODER_EVENT_SPECS.size() == static_cast<size_t>(DecoderEventId::Max));

public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the functions required by the Base class /////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Get all encoder function schemas.
     *
     * @return std::span<const FunctionSchema> Static schema span.
     */
    static std::span<const FunctionSchema> get_encoder_function_schemas()
    {
        static std::array<FunctionSchema, ENCODER_FUNCTION_SPECS.size()> schemas;
        static const bool initialized = [] {
            detail::static_schema::materialize_function_schemas(ENCODER_FUNCTION_SPECS, schemas);
            return true;
        }();
        static_cast<void>(initialized);
        return schemas;
    }
    /**
     * @brief Get all encoder event schemas.
     *
     * @return std::span<const EventSchema> Static schema span.
     */
    static std::span<const EventSchema> get_encoder_event_schemas()
    {
        static std::array<EventSchema, ENCODER_EVENT_SPECS.size()> schemas;
        static const bool initialized = [] {
            detail::static_schema::materialize_event_schemas(ENCODER_EVENT_SPECS, schemas);
            return true;
        }();
        static_cast<void>(initialized);
        return schemas;
    }

    /**
     * @brief Get all decoder function schemas.
     *
     * @return std::span<const FunctionSchema> Static schema span.
     */
    static std::span<const FunctionSchema> get_decoder_function_schemas()
    {
        static std::array<FunctionSchema, DECODER_FUNCTION_SPECS.size()> schemas;
        static const bool initialized = [] {
            detail::static_schema::materialize_function_schemas(DECODER_FUNCTION_SPECS, schemas);
            return true;
        }();
        static_cast<void>(initialized);
        return schemas;
    }
    /**
     * @brief Get all decoder event schemas.
     *
     * @return std::span<const EventSchema> Static schema span.
     */
    static std::span<const EventSchema> get_decoder_event_schemas()
    {
        static std::array<EventSchema, DECODER_EVENT_SPECS.size()> schemas;
        static const bool initialized = [] {
            detail::static_schema::materialize_event_schemas(DECODER_EVENT_SPECS, schemas);
            return true;
        }();
        static_cast<void>(initialized);
        return schemas;
    }
};

template <int Id>
class VideoEncoder: public Base<VideoEncoder<Id>> {
public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the types required by the Base class /////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Re-exported function id enum for encoder service instance.
     */
    using FunctionId = Video::EncoderFunctionId;
    /**
     * @brief Re-exported event id enum for encoder service instance.
     */
    using EventId = Video::EncoderEventId;

    /**
     * @brief Get service name of this encoder instance.
     *
     * @return std::string_view Service name in format `VideoEncoder<Id>`.
     */
    static std::string_view get_name()
    {
        static const std::string name = std::string(Video::ENCODER_NAME_PREFIX) + std::to_string(Id);
        return name;
    }

    /**
     * @brief Get function schemas for this encoder instance.
     *
     * @return std::span<const FunctionSchema> Static schema span.
     */
    static std::span<const FunctionSchema> get_function_schemas()
    {
        return Video::get_encoder_function_schemas();
    }

    /**
     * @brief Get event schemas for this encoder instance.
     *
     * @return std::span<const EventSchema> Static schema span.
     */
    static std::span<const EventSchema> get_event_schemas()
    {
        return Video::get_encoder_event_schemas();
    }
};

template <int Id>
class VideoDecoder: public Base<VideoDecoder<Id>> {
public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the types required by the Base class /////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Re-exported function id enum for decoder service instance.
     */
    using FunctionId = Video::DecoderFunctionId;
    /**
     * @brief Re-exported event id enum for decoder service instance.
     */
    using EventId = Video::DecoderEventId;

    /**
     * @brief Get service name of this decoder instance.
     *
     * @return std::string_view Service name in format `VideoDecoder<Id>`.
     */
    static std::string_view get_name()
    {
        static const std::string name = std::string(Video::DECODER_NAME_PREFIX) + std::to_string(Id);
        return name;
    }

    /**
     * @brief Get function schemas for this decoder instance.
     *
     * @return std::span<const FunctionSchema> Static schema span.
     */
    static std::span<const FunctionSchema> get_function_schemas()
    {
        return Video::get_decoder_function_schemas();
    }

    /**
     * @brief Get event schemas for this decoder instance.
     *
     * @return std::span<const EventSchema> Static schema span.
     */
    static std::span<const EventSchema> get_event_schemas()
    {
        return Video::get_decoder_event_schemas();
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the describe macros //////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief  Function related
 */
BROOKESIA_DESCRIBE_ENUM(Video::EncoderFunctionId, Open, Close, Start, Stop, FetchFrame, Max);
BROOKESIA_DESCRIBE_ENUM(Video::DecoderFunctionId, Open, Close, Start, Stop, FeedFrame, Max);

/**
 * @brief  Event related
 */
BROOKESIA_DESCRIBE_ENUM(Video::EncoderEventId, StreamSinkFrameReady, FetchSinkFrameReady, Max);
BROOKESIA_DESCRIBE_ENUM(Video::DecoderEventId, SinkFrameReady, Max);
BROOKESIA_DESCRIBE_STRUCT(
    Video::EncoderDisplayConfig, (),
    (output_name, source_name, source_role, x, y, draw_timeout_ms, publish_sink_event, activate_source, sink_index)
);
BROOKESIA_DESCRIBE_STRUCT(Video::EncoderConfig, (), (sinks, enable_stream_mode, source, display));
BROOKESIA_DESCRIBE_STRUCT(
    Video::DecoderDisplayConfig, (),
    (output_name, source_name, source_role, x, y, draw_timeout_ms, publish_sink_event)
);
BROOKESIA_DESCRIBE_STRUCT(
    Video::DecoderConfig, (),
    (width, height, source_format, sink_format, enable_stream_mode, enable_hw_acceleration, display)
);

} // namespace esp_brookesia::service::helper
