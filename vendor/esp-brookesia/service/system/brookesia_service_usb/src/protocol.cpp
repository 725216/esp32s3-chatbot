/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "protocol.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

#include "brookesia/service_usb/macro_configs.h"

namespace esp_brookesia::service::usb_internal {

namespace {

constexpr std::array<uint8_t, 2> FRAME_MAGIC = {'B', 'U'};
constexpr size_t HEADER_WITHOUT_CRC_SIZE = 20;

uint32_t read_u32_le(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

} // namespace

uint32_t crc32(std::span<const uint8_t> data)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (const uint8_t value : data) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

void FrameParser::append(std::span<const uint8_t> data)
{
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

void FrameParser::clear()
{
    buffer_.clear();
}

bool FrameParser::empty() const
{
    return buffer_.empty();
}

std::expected<std::optional<BinaryFrame>, std::string> FrameParser::next(uint32_t max_payload)
{
    if (buffer_.size() < BROOKESIA_SERVICE_USB_FRAME_HEADER_SIZE) {
        return std::nullopt;
    }

    if (buffer_[0] != FRAME_MAGIC[0] || buffer_[1] != FRAME_MAGIC[1]) {
        return std::unexpected("bad frame magic");
    }

    const uint8_t version = buffer_[2];
    if (version != BROOKESIA_SERVICE_USB_PROTOCOL_VERSION) {
        return std::unexpected("unsupported frame version");
    }

    const uint32_t payload_length = read_u32_le(buffer_.data() + 12);
    if (payload_length > max_payload) {
        return std::unexpected("frame payload is too large");
    }

    const uint32_t expected_header_crc = read_u32_le(buffer_.data() + HEADER_WITHOUT_CRC_SIZE);
    const uint32_t actual_header_crc = crc32(
                                           std::span<const uint8_t>(buffer_.data(), HEADER_WITHOUT_CRC_SIZE)
                                       );
    if (expected_header_crc != actual_header_crc) {
        return std::unexpected("bad frame header crc");
    }

    const size_t frame_size = BROOKESIA_SERVICE_USB_FRAME_HEADER_SIZE + payload_length;
    if (buffer_.size() < frame_size) {
        return std::nullopt;
    }

    const uint32_t expected_payload_crc = read_u32_le(buffer_.data() + 16);
    const auto payload = std::span<const uint8_t>(
                             buffer_.data() + BROOKESIA_SERVICE_USB_FRAME_HEADER_SIZE,
                             payload_length
                         );
    if (expected_payload_crc != crc32(payload)) {
        return std::unexpected("bad frame payload crc");
    }

    const auto frame_type = static_cast<FrameType>(buffer_[3]);
    if (frame_type != FrameType::Data && frame_type != FrameType::End && frame_type != FrameType::Cancel) {
        return std::unexpected("unsupported frame type");
    }
    if (frame_type != FrameType::Data && payload_length != 0) {
        return std::unexpected("control frame must not contain a payload");
    }

    BinaryFrame frame{
        .type = frame_type,
        .version = version,
        .request_id = read_u32_le(buffer_.data() + 4),
        .sequence = read_u32_le(buffer_.data() + 8),
        .payload = std::vector<uint8_t>(payload.begin(), payload.end()),
    };
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
    return frame;
}

std::expected<boost::json::object, std::string> parse_command_line(std::string_view line)
{
    boost::system::error_code error_code;
    auto value = boost::json::parse(line, error_code);
    if (error_code || !value.is_object()) {
        return std::unexpected(error_code ? error_code.message() : "command must be a JSON object");
    }
    return value.as_object();
}

std::string sha256_to_hex(std::span<const uint8_t> digest)
{
    static constexpr char HEX[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const uint8_t value : digest) {
        result.push_back(HEX[value >> 4]);
        result.push_back(HEX[value & 0x0F]);
    }
    return result;
}

} // namespace esp_brookesia::service::usb_internal
