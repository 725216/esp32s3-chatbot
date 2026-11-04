/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "boost/json.hpp"

namespace esp_brookesia::service::usb_internal {

enum class FrameType : uint8_t {
    Data = 1,
    End = 2,
    Cancel = 3,
};

struct BinaryFrame {
    FrameType type = FrameType::Data;
    uint8_t version = 0;
    uint32_t request_id = 0;
    uint32_t sequence = 0;
    std::vector<uint8_t> payload;
};

/**
 * @brief Incremental parser for the USB binary frame format.
 */
class FrameParser {
public:
    void append(std::span<const uint8_t> data);
    void clear();
    bool empty() const;

    /**
     * @brief Parse one frame when enough bytes are buffered.
     *
     * @return A frame, an empty optional for incomplete input, or a protocol error.
     */
    std::expected<std::optional<BinaryFrame>, std::string> next(uint32_t max_payload);

private:
    std::vector<uint8_t> buffer_;
};

uint32_t crc32(std::span<const uint8_t> data);

std::expected<boost::json::object, std::string> parse_command_line(std::string_view line);

std::string sha256_to_hex(std::span<const uint8_t> digest);

} // namespace esp_brookesia::service::usb_internal
