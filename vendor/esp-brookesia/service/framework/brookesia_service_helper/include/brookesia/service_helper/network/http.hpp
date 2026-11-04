/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <array>
#include <span>
#include <string>
#include <vector>

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/hal_interface/interfaces/network/http_client.hpp"
#include "brookesia/service_manager/detail/static_schema.hpp"
#include "brookesia/service_manager/helper/base.hpp"

namespace esp_brookesia::service::helper {

/**
 * @brief Helper schema definitions for the HTTP service.
 */
class Http: public Base<Http> {
public:
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////// The following are the service specific types and enumerations ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    using HttpMethod = hal::network::HttpClientIface::Method;
    using TlsVerifyMode = hal::network::HttpClientIface::TlsVerifyMode;
    using ErrorCode = hal::network::HttpClientIface::ErrorCode;
    using HttpRequest = hal::network::HttpClientIface::Request;
    using HttpResponse = hal::network::HttpClientIface::Response;

    /**
     * @brief Lifecycle state of an individual HTTP request.
     */
    enum class RequestState {
        Queued,
        Running,
        Completed,
        Failed,
        Canceled,
        Max,
    };

    /**
     * @brief General lifecycle state of the HTTP service.
     */
    enum class GeneralState {
        Idle,
        Initing,
        Inited,
        TimeSyncing,
        Starting,
        Started,
        Stopping,
        Stopped,
        Deiniting,
        Max,
    };

    /**
     * @brief Progress payload for a running HTTP request.
     */
    struct RequestProgress {
        uint64_t request_id = 0;
        uint32_t downloaded = 0;
        uint32_t total = 0;
    };

    /**
     * @brief Snapshot of the current state and progress of an HTTP request.
     */
    struct RequestStateInfo {
        uint64_t request_id = 0;
        RequestState state = RequestState::Queued;
        ErrorCode error = ErrorCode::Ok;
        int status_code = 0;
        uint32_t downloaded = 0;
        uint32_t total = 0;
    };

    /**
     * @brief HTTP service request counters.
     */
    struct Statistics {
        uint64_t submitted_count = 0;
        uint64_t completed_count = 0;
        uint64_t failed_count = 0;
        uint64_t canceled_count = 0;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////// The following are the types required by the Base class /////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    enum class FunctionId {
        Request,
        RequestAsync,
        CancelRequest,
        GetRequestState,
        ResetStatistics,
        Max,
    };

    enum class EventId {
        RequestStarted,
        RequestProgress,
        RequestCompleted,
        RequestFailed,
        RequestCanceled,
        GeneralStateChanged,
        Max,
    };







private:
    using FunctionParameterSpec = detail::static_schema::FunctionParameterSpec;
    using FunctionReturnSpec = detail::static_schema::FunctionReturnSpec;
    using FunctionSpec = detail::static_schema::FunctionSpec;

    inline static constexpr std::array<FunctionParameterSpec, 1> REQUEST_PARAMETERS = {{
            {
                .name = "Request",
                .description = "HTTP request object.",
                .type = FunctionValueType::Object,
            },
        }
    };

    inline static constexpr std::array<FunctionParameterSpec, 1> REQUEST_ID_PARAMETERS = {{
            {
                .name = "RequestId",
                .description = "Request id returned by RequestAsync.",
                .type = FunctionValueType::Number,
            },
        }
    };

    inline static constexpr std::array<FunctionSpec, static_cast<size_t>(FunctionId::Max)> FUNCTION_SPECS = {{
            {
                .name = "Request",
                .description = "Submit an HTTP request and wait for the response.",
                .parameters = REQUEST_PARAMETERS,
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Object,
                    .description =
                    "Example: {\"request_id\":1,\"status_code\":200,\"headers\":{},\"body\":\"{\\\"ok\\\":true}\","
                    "\"file_path\":\"\",\"error\":\"Ok\",\"error_message\":\"\"}",
                },
            },
            {
                .name = "RequestAsync",
                .description = "Submit an HTTP request asynchronously.",
                .parameters = REQUEST_PARAMETERS,
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Number,
                    .description = "Request id number.",
                },
            },
            {
                .name = "CancelRequest",
                .description = "Cancel a pending or running HTTP request.",
                .parameters = REQUEST_ID_PARAMETERS,
                .require_scheduler = false,
            },
            {
                .name = "GetRequestState",
                .description = "Get HTTP request state.",
                .parameters = REQUEST_ID_PARAMETERS,
                .require_scheduler = false,
                .return_value = FunctionReturnSpec{
                    .type = FunctionValueType::Object,
                    .description =
                    "Example: {\"request_id\":1,\"state\":\"Completed\",\"error\":\"Ok\",\"status_code\":200,"
                    "\"downloaded\":128,\"total\":128}",
                },
            },
            {
                .name = "ResetStatistics",
                .description = "Reset HTTP service statistics.",
                .parameters = std::span<const FunctionParameterSpec>{},
                .require_scheduler = false,
            },
        }
    };
    static_assert(FUNCTION_SPECS.size() == static_cast<size_t>(FunctionId::Max));

    using EventItemSpec = detail::static_schema::EventItemSpec;
    using EventSpec = detail::static_schema::EventSpec;

    inline static constexpr std::array<EventItemSpec, 2> REQUEST_ID_STATE_ITEMS = {{
            {"RequestId", "Request id.", EventItemType::Number},
            {"State", "Request state.", EventItemType::String},
        }
    };

    inline static constexpr std::array<EventItemSpec, 2> REQUEST_ID_PROGRESS_ITEMS = {{
            {"RequestId", "Request id.", EventItemType::Number},
            {"Progress", "Progress object.", EventItemType::Object},
        }
    };

    inline static constexpr std::array<EventItemSpec, 2> REQUEST_ID_RESPONSE_ITEMS = {{
            {"RequestId", "Request id.", EventItemType::Number},
            {"Response", "HTTP response object.", EventItemType::Object},
        }
    };

    inline static constexpr std::array<EventItemSpec, 1> GENERAL_STATE_CHANGED_ITEMS = {{
            {"State", "General state.", EventItemType::String},
        }
    };

    inline static constexpr std::array<EventSpec, static_cast<size_t>(EventId::Max)> EVENT_SPECS = {{
            {
                .name = "RequestStarted",
                .description = "HTTP request started.",
                .items = REQUEST_ID_STATE_ITEMS,
            },
            {
                .name = "RequestProgress",
                .description = "HTTP request progress updated.",
                .items = REQUEST_ID_PROGRESS_ITEMS,
            },
            {
                .name = "RequestCompleted",
                .description = "HTTP request completed.",
                .items = REQUEST_ID_RESPONSE_ITEMS,
            },
            {
                .name = "RequestFailed",
                .description = "HTTP request failed.",
                .items = REQUEST_ID_RESPONSE_ITEMS,
            },
            {
                .name = "RequestCanceled",
                .description = "HTTP request canceled.",
                .items = REQUEST_ID_RESPONSE_ITEMS,
            },
            {
                .name = "GeneralStateChanged",
                .description = "HTTP service general state changed.",
                .items = GENERAL_STATE_CHANGED_ITEMS,
            },
        }
    };
    static_assert(EVENT_SPECS.size() == static_cast<size_t>(EventId::Max));

public:
    static constexpr std::string_view get_name()
    {
        return "Http";
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

BROOKESIA_DESCRIBE_ENUM(Http::RequestState, Queued, Running, Completed, Failed, Canceled, Max);
BROOKESIA_DESCRIBE_ENUM(
    Http::GeneralState, Idle, Initing, Inited, TimeSyncing, Starting, Started, Stopping, Stopped, Deiniting, Max
);
BROOKESIA_DESCRIBE_ENUM(Http::FunctionId, Request, RequestAsync, CancelRequest, GetRequestState, ResetStatistics, Max);
BROOKESIA_DESCRIBE_ENUM(
    Http::EventId, RequestStarted, RequestProgress, RequestCompleted, RequestFailed, RequestCanceled,
    GeneralStateChanged, Max
);
BROOKESIA_DESCRIBE_STRUCT(Http::RequestProgress, (), (request_id, downloaded, total));
BROOKESIA_DESCRIBE_STRUCT(Http::RequestStateInfo, (), (request_id, state, error, status_code, downloaded, total));
BROOKESIA_DESCRIBE_STRUCT(
    Http::Statistics, (), (submitted_count, completed_count, failed_count, canceled_count)
);

} // namespace esp_brookesia::service::helper
