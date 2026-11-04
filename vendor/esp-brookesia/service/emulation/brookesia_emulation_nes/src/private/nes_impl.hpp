#pragma once

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "brookesia/lib_utils/function_guard.hpp"
#include "brookesia/lib_utils/plugin.hpp"
#include "brookesia/service_manager/dataflow/registry.hpp"
#include "brookesia/service_manager/service/manager.hpp"
#include "brookesia/emulation_nes/emulation_nes.hpp"

extern "C" {
#include "nofrendo.h"
#include "nes/input.h"
#include "nes/state.h"
}

#if !BROOKESIA_EMULATION_NES_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/utils.hpp"

namespace esp_brookesia::emulation {

namespace nes_detail {

namespace dataflow = service::dataflow;

constexpr std::string_view DEFAULT_DISPLAY_SOURCE_ROLE = "game";
constexpr uint32_t NES_NATIVE_WIDTH = 256;
constexpr uint32_t NES_NATIVE_HEIGHT = 240;
constexpr uint32_t NES_VISIBLE_HEIGHT = 224;
constexpr uint32_t NES_VISIBLE_Y_OFFSET = 8;
constexpr uint32_t RGB565_BYTES_PER_PIXEL = 2;
constexpr size_t NES_VIDEO_BUFFER_COUNT = 2;
constexpr size_t NES_PRESENT_BUFFER_COUNT = 3;
constexpr uint8_t NES_AUDIO_SAMPLE_BITS = 16;
constexpr uint8_t NES_AUDIO_FRAME_DURATION_MS = 16;
constexpr uint32_t NES_AUDIO_FEED_INTERVAL_MS = 4;
constexpr const char *NES_AUDIO_SOURCE_NAME = "NES";
constexpr const char *NES_AUDIO_SOURCE_ROLE = "game";
constexpr const char *NES_AUDIO_OUTPUT_NAME = "Speaker0";
constexpr size_t NES_ROM_MAX_SIZE = 0x200000;
constexpr int NES_TASK_STOP_TIMEOUT_MS = 3000;
constexpr int NES_PRESENT_DRAIN_EXTRA_TIMEOUT_MS = 1000;
const lib_utils::TaskSchedulerGroup NES_FRAME_TASK_GROUP = "Frame";
const lib_utils::TaskSchedulerGroup NES_AUDIO_TASK_GROUP = "Audio";

constexpr uint32_t get_auto_frame_skip_max()
{
    return std::max<uint32_t>(
               BROOKESIA_EMULATION_NES_AUTO_FRAME_SKIP_MAX,
               BROOKESIA_EMULATION_NES_MAX_CONSECUTIVE_FRAME_SKIP
           );
}

inline std::string to_string(Nes::State state)
{
    return std::string(BROOKESIA_DESCRIBE_ENUM_TO_STR(state));
}

inline int64_t get_current_time_ms()
{
    using namespace std::chrono;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           ).count();
}

inline std::expected<void, std::string> ensure_path_exists(const std::string &path)
{
    if (path.empty()) {
        return std::unexpected("ROM path is empty");
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return std::unexpected("ROM path does not exist: " + path);
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return std::unexpected("ROM path is not a regular file: " + path);
    }
    return {};
}

inline std::string map_rom_load_error(int error)
{
    switch (error) {
    case -1:
        return "Failed to load NES ROM";
    case -2:
        return "Unsupported NES mapper";
    case -3:
        return "NES BIOS file is required";
    default:
        return "Unsupported NES ROM";
    }
}

inline int gamepad_to_nes_buttons(Nes::GamepadState state)
{
    int buttons = 0;
    if (state.a) {
        buttons |= NES_PAD_A;
    }
    if (state.b) {
        buttons |= NES_PAD_B;
    }
    if (state.select) {
        buttons |= NES_PAD_SELECT;
    }
    if (state.start) {
        buttons |= NES_PAD_START;
    }

    if (state.up != state.down) {
        buttons |= state.up ? NES_PAD_UP : NES_PAD_DOWN;
    }
    if (state.left != state.right) {
        buttons |= state.left ? NES_PAD_LEFT : NES_PAD_RIGHT;
    }
    return buttons;
}

struct FreeDeleter {
    void operator()(void *ptr) const
    {
        std::free(ptr);
    }
};

using OwnedBuffer = std::unique_ptr<uint8_t, FreeDeleter>;

inline void log_buffer_allocation(std::string_view label, const void *ptr, size_t size)
{
#if BROOKESIA_EMULATION_NES_LOG_BUFFER_MEMORY
    BROOKESIA_LOGI(
        "NES buffer: name=%1%, size=%2%, ptr=%3%",
        label, size, ptr
    );
#else
    (void)label;
    (void)ptr;
    (void)size;
#endif
}

inline std::expected<OwnedBuffer, std::string> allocate_buffer(size_t size, std::string_view label)
{
    if (size == 0) {
        return std::unexpected("Buffer size is zero");
    }

    auto *ptr = std::malloc(size);
    if (ptr == nullptr) {
        return std::unexpected("Failed to allocate buffer: " + std::string(label));
    }

    OwnedBuffer buffer(static_cast<uint8_t *>(ptr));
    log_buffer_allocation(label, buffer.get(), size);
    return buffer;
}

class HeapBuffer {
public:
    HeapBuffer() = default;
    HeapBuffer(HeapBuffer &&) noexcept = default;
    HeapBuffer &operator=(HeapBuffer &&) noexcept = default;

    HeapBuffer(const HeapBuffer &) = delete;
    HeapBuffer &operator=(const HeapBuffer &) = delete;

    std::expected<void, std::string> resize(size_t size, std::string_view label)
    {
        if ((size_ == size) && data_) {
            return {};
        }

        auto buffer_result = allocate_buffer(size, label);
        if (!buffer_result) {
            return std::unexpected(buffer_result.error());
        }
        data_ = std::move(buffer_result.value());
        size_ = size;
        return {};
    }

    void clear()
    {
        data_.reset();
        size_ = 0;
    }

    void swap(HeapBuffer &other) noexcept
    {
        data_.swap(other.data_);
        std::swap(size_, other.size_);
    }

    uint8_t *data()
    {
        return data_.get();
    }

    const uint8_t *data() const
    {
        return data_.get();
    }

    size_t size() const
    {
        return size_;
    }

    bool empty() const
    {
        return (data_ == nullptr) || (size_ == 0);
    }

private:
    OwnedBuffer data_;
    size_t size_ = 0;
};

inline std::expected<std::pair<OwnedBuffer, size_t>, std::string> load_file_to_memory(const std::string &path)
{
    auto path_result = ensure_path_exists(path);
    if (!path_result) {
        return std::unexpected(path_result.error());
    }

    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::unexpected("Failed to get ROM file size: " + ec.message());
    }
    if (file_size < 16 || file_size > NES_ROM_MAX_SIZE) {
        return std::unexpected("Invalid ROM file size: " + std::to_string(file_size));
    }

    auto buffer_result = allocate_buffer(static_cast<size_t>(file_size), "rom");
    if (!buffer_result) {
        return std::unexpected(buffer_result.error() + ": " + path);
    }
    auto buffer = std::move(buffer_result.value());

    auto *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        return std::unexpected("Failed to open ROM path: " + path);
    }
    lib_utils::FunctionGuard close_guard([fp]() {
        std::fclose(fp);
    });

    const auto file_size_bytes = static_cast<size_t>(file_size);
    const auto read_count = std::fread(buffer.get(), 1, file_size_bytes, fp);
    if (read_count != file_size_bytes) {
        return std::unexpected("Failed to read complete ROM file: " + path);
    }

    return std::make_pair(std::move(buffer), file_size_bytes);
}

} // namespace nes_detail

using namespace nes_detail;

class Nes::Runtime: public std::enable_shared_from_this<Nes::Runtime> {
public:
    struct PerfSnapshot {
        Nes::AudioMode audio_mode = Nes::AudioMode::Disabled;
        bool audio_started = false;
        size_t audio_queue_depth = 0;
        uint32_t audio_drop_count = 0;
        uint32_t render_width = 0;
        uint32_t render_height = 0;
        Nes::VideoMode video_mode = Nes::VideoMode::Fit;
        uint32_t display_backpressure_count = 0;
        uint32_t present_drop_count = 0;
        uint32_t present_error_count = 0;
    };

    std::expected<void, std::string> load(const Config &config)
    {
        std::lock_guard lock(runtime_mutex_);

        auto path_result = ensure_path_exists(config.rom_path);
        if (!path_result) {
            return path_result;
        }

        config_ = config;
        unload_locked();

        auto display_result = setup_display();
        if (!display_result) {
            return display_result;
        }

        auto emulator_result = setup_emulator();
        if (!emulator_result) {
            unload_locked();
            return emulator_result;
        }

        auto audio_result = setup_audio();
        if (!audio_result) {
            unload_locked();
            return audio_result;
        }

        BROOKESIA_LOGI(
            "Loaded NES ROM path=%1%, output=%2%, source=%3%", config_.rom_path, output_name_,
            config_.display_source_name
        );
        return {};
    }

    void unload()
    {
        std::lock_guard lock(runtime_mutex_);
        unload_locked();
    }

    std::expected<void, std::string> reset()
    {
        std::lock_guard lock(emulator_mutex_);
        if (nes_ == nullptr) {
            return std::unexpected("NES runtime is not loaded");
        }
        nes_reset(true);
        return {};
    }

    std::expected<void, std::string> save()
    {
        if (config_.save_path.empty()) {
            return {};
        }

        std::filesystem::path save_path(config_.save_path);
        std::error_code ec;
        if (save_path.has_parent_path()) {
            std::filesystem::create_directories(save_path.parent_path(), ec);
            if (ec) {
                return std::unexpected("Failed to create save directory: " + ec.message());
            }
        }

        {
            std::lock_guard lock(emulator_mutex_);
            if (state_save(config_.save_path.c_str()) != 0) {
                return std::unexpected("Failed to write save path: " + config_.save_path);
            }
        }
        return {};
    }

    std::expected<void, std::string> set_gamepad_state(GamepadState state)
    {
        std::lock_guard lock(gamepad_mutex_);
        gamepad_state_ = state;
        return {};
    }

    PerfSnapshot get_perf_snapshot()
    {
        PerfSnapshot snapshot;
        {
            std::lock_guard lock(runtime_mutex_);
            snapshot.audio_mode = config_.audio_mode;
            snapshot.render_width = render_width_;
            snapshot.render_height = render_height_;
            snapshot.video_mode = config_.video_mode;
        }
        {
            std::lock_guard lock(audio_queue_mutex_);
            snapshot.audio_started = audio_started_;
            snapshot.audio_queue_depth = audio_queue_count_;
            snapshot.audio_drop_count = audio_drop_count_;
        }
        {
            std::lock_guard lock(present_mutex_);
            snapshot.display_backpressure_count = display_backpressure_count_;
            snapshot.present_drop_count = present_drop_count_;
            snapshot.present_error_count = present_error_count_;
        }
        return snapshot;
    }

    bool is_audio_started() const
    {
        std::lock_guard lock(audio_queue_mutex_);
        return audio_started_;
    }

    bool consume_display_backpressure()
    {
        std::lock_guard lock(present_mutex_);
        const bool pending = display_backpressure_pending_;
        display_backpressure_pending_ = false;
        return pending;
    }

    bool step_frame(bool draw_video, Nes::FrameStepStats *stats)
    {
        if (stats != nullptr) {
            *stats = {};
            stats->drew_video = draw_video;
        }

        std::shared_ptr<dataflow::VisualOperation> visual_operation;
        std::string output_name;
        uint32_t output_x = 0;
        uint32_t output_y = 0;
        uint32_t render_width = 0;
        uint32_t render_height = 0;
        bool output_is_buffer = false;
        std::optional<size_t> present_buffer_index;
        {
            std::lock_guard lock(runtime_mutex_);
            if (nes_ == nullptr) {
                return false;
            }
            if (draw_video) {
                if (!visual_operation_) {
                    return false;
                }
                visual_operation = visual_operation_;
                output_name = output_name_;
                output_x = output_x_;
                output_y = output_y_;
                render_width = render_width_;
                render_height = render_height_;
                output_is_buffer = output_is_buffer_;
            }
        }
        if (draw_video && !output_is_buffer) {
            present_buffer_index = reserve_present_buffer(
                                       static_cast<size_t>(render_width) * render_height * RGB565_BYTES_PER_PIXEL
                                   );
            if (!present_buffer_index.has_value()) {
                draw_video = false;
            }
        }
        if (stats != nullptr) {
            stats->drew_video = draw_video;
        }

        const uint8_t *frame_vidbuf = nullptr;
        {
            std::lock_guard lock(emulator_mutex_);
            if (nes_ == nullptr) {
                if (present_buffer_index.has_value()) {
                    release_present_buffer(present_buffer_index.value(), dataflow::VisualPresentResult::Error);
                }
                return false;
            }
            if (draw_video) {
                nes_vidbuf_index_ = (nes_vidbuf_index_ + 1) % nes_vidbufs_.size();
            }
            frame_vidbuf = nes_vidbufs_[nes_vidbuf_index_].data();
            nes_setvidbuf(const_cast<uint8_t *>(frame_vidbuf));
            {
                std::lock_guard gamepad_lock(gamepad_mutex_);
                input_update(0, gamepad_to_nes_buttons(gamepad_state_));
                input_update(1, 0);
            }
            const auto emulate_start_ms = get_current_time_ms();
            nes_emulate(draw_video);
            if (stats != nullptr) {
                stats->emulate_ms = static_cast<uint32_t>(get_current_time_ms() - emulate_start_ms);
            }
            enqueue_audio_frame();
        }

        if (!draw_video) {
            return true;
        }
        if (frame_vidbuf == nullptr) {
            if (present_buffer_index.has_value()) {
                release_present_buffer(present_buffer_index.value(), dataflow::VisualPresentResult::Error);
            }
            return false;
        }

        dataflow::VisualFrameInfo frame{
            .x = output_x,
            .y = output_y,
            .width = render_width,
            .height = render_height,
            .pixel_format = dataflow::VisualPixelFormat::RGB565,
        };
        dataflow::VisualPresentResult result = dataflow::VisualPresentResult::Error;
        if (output_is_buffer) {
            const auto present_start_ms = get_current_time_ms();
            auto buffer_writer = [this, frame_vidbuf, &frame, stats](dataflow::VisualBufferView & view) {
                if (view.data.empty()) {
                    return false;
                }
                auto *output_data = view.data.data();
                const auto convert_start_ms = get_current_time_ms();
                const size_t dst_offset = static_cast<size_t>(frame.y) * view.stride_bytes +
                                          static_cast<size_t>(frame.x) * RGB565_BYTES_PER_PIXEL;
                convert_frame_to(frame_vidbuf, output_data + dst_offset, view.stride_bytes);
                if (stats != nullptr) {
                    stats->convert_ms = static_cast<uint32_t>(get_current_time_ms() - convert_start_ms);
                }
                return true;
            };
            result = visual_operation->present_buffer_frame_sync(output_name, frame, std::move(buffer_writer));
            if (stats != nullptr) {
                stats->present_ms = static_cast<uint32_t>(get_current_time_ms() - present_start_ms);
            }
        } else {
            if (!present_buffer_index.has_value()) {
                return true;
            }
            auto &present_buffer = present_buffers_[present_buffer_index.value()];
            const auto convert_start_ms = get_current_time_ms();
            convert_frame_to(
                frame_vidbuf, present_buffer.data.data(),
                static_cast<size_t>(render_width_) * RGB565_BYTES_PER_PIXEL
            );
            if (stats != nullptr) {
                stats->convert_ms = static_cast<uint32_t>(get_current_time_ms() - convert_start_ms);
            }
            const auto present_start_ms = get_current_time_ms();
            auto weak_runtime = weak_from_this();
            auto on_complete = [weak_runtime, buffer_index = present_buffer_index.value()](
                                   uint32_t, dataflow::VisualPresentResult complete_result
            ) {
                if (auto runtime = weak_runtime.lock()) {
                    runtime->release_present_buffer(buffer_index, complete_result);
                }
            };
            auto submit_result = visual_operation->present_frame_async(
                                     output_name, frame,
                                     std::span<const uint8_t>(present_buffer.data.data(), present_buffer.data.size()),
                                     std::move(on_complete),
                                     BROOKESIA_EMULATION_NES_DRAW_TIMEOUT_MS
                                 );
            switch (submit_result.state) {
            case dataflow::VisualPresentSubmitState::Queued:
                result = dataflow::VisualPresentResult::Presented;
                break;
            case dataflow::VisualPresentSubmitState::DroppedNotActive:
                release_present_buffer(present_buffer_index.value(), dataflow::VisualPresentResult::DroppedNotActive);
                result = dataflow::VisualPresentResult::DroppedNotActive;
                break;
            case dataflow::VisualPresentSubmitState::DroppedInvalidFrame:
                release_present_buffer(present_buffer_index.value(), dataflow::VisualPresentResult::DroppedInvalidFrame);
                result = dataflow::VisualPresentResult::DroppedInvalidFrame;
                break;
            case dataflow::VisualPresentSubmitState::Error:
            default:
                release_present_buffer(present_buffer_index.value(), dataflow::VisualPresentResult::Error);
                result = dataflow::VisualPresentResult::Error;
                break;
            }
            if (stats != nullptr) {
                stats->present_ms = static_cast<uint32_t>(get_current_time_ms() - present_start_ms);
            }
        }
        if ((result != dataflow::VisualPresentResult::Presented) &&
                (result != dataflow::VisualPresentResult::DroppedNotActive)) {
            BROOKESIA_LOGW("Failed to present NES frame: result(%1%)", static_cast<int>(result));
        }
        return true;
    }

    bool flush_audio_queue(Nes::AudioStepStats *stats)
    {
        if (stats != nullptr) {
            *stats = {};
        }

        const auto flush_start_ms = get_current_time_ms();
        for (uint32_t fed_chunks = 0; fed_chunks < BROOKESIA_EMULATION_NES_AUDIO_FEED_MAX_CHUNKS_PER_TICK;
                ++fed_chunks) {
            size_t feed_size = 0;
            {
                std::lock_guard lock(audio_queue_mutex_);
                if (!audio_started_ || (audio_queue_count_ == 0)) {
                    if (stats != nullptr) {
                        stats->elapsed_ms = static_cast<uint32_t>(get_current_time_ms() - flush_start_ms);
                    }
                    return true;
                }
                auto &chunk = audio_queue_[audio_queue_head_];
                audio_feed_buffer_.swap(chunk.data);
                feed_size = chunk.size;
                chunk.size = 0;
                audio_queue_head_ = (audio_queue_head_ + 1) % audio_queue_.size();
                audio_queue_count_--;
            }

            auto audio_operation = audio_operation_;
            if (!audio_operation) {
                if (stats != nullptr) {
                    stats->elapsed_ms = static_cast<uint32_t>(get_current_time_ms() - flush_start_ms);
                }
                return true;
            }

            struct WaitContext {
                std::mutex mutex;
                std::condition_variable cv;
                dataflow::AudioWriteResult result = dataflow::AudioWriteResult::Error;
                bool done = false;
            };
            WaitContext wait_context;
            auto release_callback = [&wait_context](dataflow::AudioWriteResult result) {
                std::lock_guard lock(wait_context.mutex);
                wait_context.result = result;
                wait_context.done = true;
                wait_context.cv.notify_one();
            };
            auto write_result = audio_operation->write_borrowed(
                                    audio_output_name_, std::span<const uint8_t>(audio_feed_buffer_.data(), feed_size),
                                    std::move(release_callback), 0
                                );
            if (write_result == dataflow::AudioWriteResult::Written) {
                std::unique_lock lock(wait_context.mutex);
                wait_context.cv.wait(lock, [&wait_context]() {
                    return wait_context.done;
                });
                write_result = wait_context.result;
                if (write_result == dataflow::AudioWriteResult::Written) {
                    if (stats != nullptr) {
                        stats->fed_chunks++;
                    }
                }
            }
            if (write_result == dataflow::AudioWriteResult::DroppedQueueFull) {
                std::lock_guard lock(audio_queue_mutex_);
                audio_drop_count_++;
                if (!audio_queue_drop_logged_) {
                    audio_queue_drop_logged_ = true;
                    BROOKESIA_LOGW("NES audio stream queue is full; dropping audio chunks");
                }
            } else if (write_result != dataflow::AudioWriteResult::DroppedNotActive) {
                bool should_log_error = false;
                {
                    std::lock_guard lock(audio_queue_mutex_);
                    if (!audio_error_logged_) {
                        audio_error_logged_ = true;
                        should_log_error = true;
                    }
                }
                if (should_log_error) {
                    BROOKESIA_LOGW(
                        "Failed to write NES audio stream data: result(%1%)", static_cast<int>(write_result)
                    );
                }
            }

            if constexpr (BROOKESIA_EMULATION_NES_AUDIO_FEED_BUDGET_MS > 0) {
                if ((get_current_time_ms() - flush_start_ms) >= BROOKESIA_EMULATION_NES_AUDIO_FEED_BUDGET_MS) {
                    break;
                }
            }
        }
        if (stats != nullptr) {
            stats->elapsed_ms = static_cast<uint32_t>(get_current_time_ms() - flush_start_ms);
        }
        return true;
    }

private:
    struct AudioChunk {
        HeapBuffer data;
        size_t size = 0;
    };
    struct PresentBuffer {
        HeapBuffer data;
        bool busy = false;
    };

    void unload_locked()
    {
        if (!wait_for_present_buffers_idle()) {
            BROOKESIA_LOGW("Timed out waiting for NES Display frames to complete");
        }
        stop_audio();
        shutdown_emulator();
        release_display();
        clear_present_buffers();
        for (auto &vidbuf : nes_vidbufs_) {
            vidbuf.clear();
        }
        nes_vidbuf_index_ = 0;
        x_src_map_.clear();
        row_src_offset_map_.clear();
        rom_buffer_.reset();
        rom_size_ = 0;
        palette_.clear();
        visual_operation_.reset();
    }

    std::expected<void, std::string> setup_display()
    {
        dataflow::VisualOperationConfig operation_config;
        operation_config.owner = "brookesia.emulation.nes";
        operation_config.source = {
            .name = config_.display_source_name.empty() ? std::string("NES") : config_.display_source_name,
            .role = std::string(DEFAULT_DISPLAY_SOURCE_ROLE),
.preferred_outputs = config_.display_output_name.empty() ? std::vector<std::string>{} :
            std::vector<std::string>{config_.display_output_name},
            .priority = 0,
        };
        operation_config.output_name = config_.display_output_name;
        auto operation_result = service::ServiceManager::get_instance().get_dataflow_registry().open_visual_operation(
                                    std::move(operation_config)
                                );
        if (!operation_result) {
            return std::unexpected("Failed to open NES visual operation: " + operation_result.error());
        }
        auto operation = std::move(operation_result.value());
        auto outputs = operation->get_outputs();
        if (outputs.empty()) {
            operation->close();
            return std::unexpected("No Display output is available");
        }

        auto output_it = outputs.begin();
        if (!config_.display_output_name.empty()) {
            output_it = std::find_if(outputs.begin(), outputs.end(), [this](const auto & output) {
                return output.output.name == config_.display_output_name;
            });
            if (output_it == outputs.end()) {
                operation->close();
                return std::unexpected("Display output is not found: " + config_.display_output_name);
            }
        }

        if (output_it->pixel_format != dataflow::VisualPixelFormat::RGB565) {
            operation->close();
            return std::unexpected("NES v1 requires RGB565 Display output");
        }

        output_name_ = output_it->output.name;
        output_width_ = output_it->output.width;
        output_height_ = output_it->output.height;
        output_is_buffer_ = output_it->slot == dataflow::VisualOutputSlot::Buffer;
        auto area_result = resolve_render_area();
        if (!area_result) {
            operation->close();
            return area_result;
        }

        auto request_result = operation->request_output(output_name_);
        if (!request_result) {
            operation->close();
            return std::unexpected("Failed to request NES Display output: " + request_result.error());
        }

        if (config_.auto_activate_display) {
            auto active_result = operation->set_active_source(output_name_);
            if (!active_result) {
                BROOKESIA_LOGW("Failed to activate NES Display source: %1%", active_result.error());
            }
        }

        if (!output_is_buffer_) {
            auto present_result = setup_present_buffers(render_width_ * render_height_ * RGB565_BYTES_PER_PIXEL);
            if (!present_result) {
                operation->close();
                return present_result;
            }
        }
        visual_operation_ = std::move(operation);
        return {};
    }

    std::expected<void, std::string> setup_emulator()
    {
        const auto load_start_ms = get_current_time_ms();
        auto rom_result = load_file_to_memory(config_.rom_path);
        if (!rom_result) {
            return std::unexpected(rom_result.error());
        }
        auto rom_image = std::move(rom_result.value());
        rom_buffer_ = std::move(rom_image.first);
        rom_size_ = rom_image.second;
        BROOKESIA_LOGI(
            "Preloaded NES ROM into memory: path=%1%, size=%2% bytes, elapsed=%3% ms", config_.rom_path,
            rom_size_, get_current_time_ms() - load_start_ms
        );

        nes_ = nes_init(
                   SYS_DETECT, BROOKESIA_EMULATION_NES_AUDIO_SAMPLE_RATE,
                   BROOKESIA_EMULATION_NES_AUDIO_STEREO != 0, nullptr
               );
        if (nes_ == nullptr) {
            return std::unexpected("Failed to initialize nofrendo");
        }

        const int load_result = nes_loadmem(rom_buffer_.get(), rom_size_, config_.rom_path.c_str());
        if (load_result < 0) {
            return std::unexpected(map_rom_load_error(load_result) + ": " + config_.rom_path);
        }

        input_connect(0, NES_JOYPAD);
        input_connect(1, NES_JOYPAD);
        ppu_setopt(PPU_LIMIT_SPRITES, 1);

        std::unique_ptr<uint8_t, FreeDeleter> built_palette(
            static_cast<uint8_t *>(nofrendo_buildpalette(NES_PALETTE_PVM, 16))
        );
        if (!built_palette) {
            return std::unexpected("Failed to build NES RGB565 palette");
        }
        constexpr size_t palette_bytes = 256 * sizeof(uint16_t);
        auto palette_result = palette_.resize(palette_bytes, "palette_rgb565");
        if (!palette_result) {
            return palette_result;
        }
        std::memcpy(palette_.data(), built_palette.get(), palette_bytes);

        for (size_t i = 0; i < nes_vidbufs_.size(); ++i) {
            auto label = "nes_vidbuf[" + std::to_string(i) + "]";
            auto resize_result = nes_vidbufs_[i].resize(NES_SCREEN_PITCH * NES_NATIVE_HEIGHT, label);
            if (!resize_result) {
                return resize_result;
            }
        }
        nes_vidbuf_index_ = 0;
        nes_setvidbuf(nes_vidbufs_[nes_vidbuf_index_].data());

        // Nofrendo needs a couple of dry frames after ROM load to settle mapper/PPU state, matching retro-go.
        nes_emulate(false);
        nes_emulate(false);
        return {};
    }

    void shutdown_emulator()
    {
        if (nes_ == nullptr) {
            return;
        }
        nes_shutdown();
        nes_ = nullptr;
    }

    std::expected<void, std::string> setup_audio()
    {
        if (config_.audio_mode == AudioMode::Disabled) {
            return {};
        }
        const uint8_t channels = (nes_ != nullptr && nes_->apu != nullptr && nes_->apu->stereo) ? 2 : 1;
        dataflow::AudioPlaybackOperationConfig operation_config;
        operation_config.owner = "brookesia.emulation.nes";
        operation_config.source = {
            .name = NES_AUDIO_SOURCE_NAME,
            .role = NES_AUDIO_SOURCE_ROLE,
            .preferred_outputs = {NES_AUDIO_OUTPUT_NAME},
            .priority = 0,
        };
        operation_config.output_name = NES_AUDIO_OUTPUT_NAME;
        operation_config.stream = {
            .type = dataflow::AudioCodecFormat::PCM,
            .general = {
                .channels = channels,
                .sample_bits = NES_AUDIO_SAMPLE_BITS,
                .sample_rate = BROOKESIA_EMULATION_NES_AUDIO_SAMPLE_RATE,
                .frame_duration = NES_AUDIO_FRAME_DURATION_MS,
            },
            .queue_size_bytes = 32 * 1024,
            .queue_policy = dataflow::AudioStreamQueuePolicy::DropNewest,
        };
        operation_config.open_stream = false;
        const auto stream_config = operation_config.stream;
        auto operation_result = service::ServiceManager::get_instance().get_dataflow_registry().open_audio_playback_operation(
                                    std::move(operation_config)
                                );
        if (!operation_result) {
            const auto message = "Audio Playback provider is not available: " + operation_result.error();
            if (config_.audio_mode == AudioMode::Required) {
                return std::unexpected(message);
            }
            BROOKESIA_LOGW("%1%; NES continues muted", message);
            return {};
        }
        audio_operation_ = std::move(operation_result.value());
        audio_output_name_ = NES_AUDIO_OUTPUT_NAME;
        lib_utils::FunctionGuard stop_audio_guard([this]() {
            stop_audio();
        });
        auto fail_audio_setup = [this](const std::string & message) -> std::expected<void, std::string> {
            if (config_.audio_mode == AudioMode::Required)
            {
                return std::unexpected(message);
            }
            BROOKESIA_LOGW("%1%; NES continues muted", message);
            return {};
        };

        auto request_result = audio_operation_->request_output(audio_output_name_);
        if (!request_result) {
            return fail_audio_setup("Failed to request NES audio output: " + request_result.error());
        }
        auto active_result = audio_operation_->set_active_source(audio_output_name_);
        if (!active_result) {
            return fail_audio_setup("Failed to set NES audio source active: " + active_result.error());
        }
        auto open_result = audio_operation_->open_stream(audio_output_name_, stream_config);
        if (!open_result) {
            return fail_audio_setup("Failed to open NES audio stream: " + open_result.error());
        }

        {
            std::lock_guard lock(audio_queue_mutex_);
            audio_chunk_bytes_ = get_audio_frame_byte_count();
            audio_queue_.clear();
            audio_queue_.resize(BROOKESIA_EMULATION_NES_AUDIO_QUEUE_MAX_FRAMES);
            for (size_t i = 0; i < audio_queue_.size(); ++i) {
                auto label = "audio_chunk[" + std::to_string(i) + "]";
                auto resize_result = audio_queue_[i].data.resize(audio_chunk_bytes_, label);
                if (!resize_result) {
                    audio_queue_.clear();
                    audio_chunk_bytes_ = 0;
                    audio_started_ = false;
                    return fail_audio_setup("Failed to allocate NES audio queue: " + resize_result.error());
                }
                auto &chunk = audio_queue_[i];
                chunk.size = 0;
            }
            auto feed_resize_result = audio_feed_buffer_.resize(audio_chunk_bytes_, "audio_feed");
            if (!feed_resize_result) {
                audio_queue_.clear();
                audio_chunk_bytes_ = 0;
                audio_started_ = false;
                return fail_audio_setup("Failed to allocate NES audio feed buffer: " + feed_resize_result.error());
            }
            audio_queue_head_ = 0;
            audio_queue_count_ = 0;
            audio_started_ = true;
            audio_error_logged_ = false;
            audio_queue_drop_logged_ = false;
            audio_drop_count_ = 0;
        }
        BROOKESIA_LOGI("NES audio started: %1% Hz, %2% channels", BROOKESIA_EMULATION_NES_AUDIO_SAMPLE_RATE, channels);
        stop_audio_guard.release();
        return {};
    }

    void stop_audio()
    {
        {
            std::lock_guard lock(audio_queue_mutex_);
            audio_started_ = false;
            audio_queue_.clear();
            audio_feed_buffer_.clear();
            audio_queue_head_ = 0;
            audio_queue_count_ = 0;
            audio_chunk_bytes_ = 0;
            audio_error_logged_ = false;
            audio_queue_drop_logged_ = false;
        }
        if (audio_operation_) {
            audio_operation_->close();
            audio_operation_.reset();
        }
        audio_output_name_.clear();
    }

    size_t get_audio_frame_byte_count() const
    {
        if ((nes_ == nullptr) || (nes_->apu == nullptr)) {
            return 0;
        }
        const size_t channels = nes_->apu->stereo ? 2 : 1;
        return static_cast<size_t>(nes_->apu->samples_per_frame) * channels * sizeof(int16_t);
    }

    void enqueue_audio_frame()
    {
        if ((nes_ == nullptr) || (nes_->apu == nullptr) || (nes_->apu->buffer == nullptr)) {
            return;
        }
        const size_t byte_count = get_audio_frame_byte_count();
        if (byte_count == 0) {
            return;
        }

        std::lock_guard lock(audio_queue_mutex_);
        if (!audio_started_ || audio_queue_.empty()) {
            return;
        }
        if (byte_count > audio_chunk_bytes_) {
            return;
        }
        while (audio_queue_count_ >= audio_queue_.size()) {
            audio_queue_head_ = (audio_queue_head_ + 1) % audio_queue_.size();
            audio_queue_count_--;
            audio_drop_count_++;
            if (!audio_queue_drop_logged_) {
                BROOKESIA_LOGW("NES audio queue is full; dropping stale audio to keep video smooth");
                audio_queue_drop_logged_ = true;
            }
        }
        const size_t write_index = (audio_queue_head_ + audio_queue_count_) % audio_queue_.size();
        auto &chunk = audio_queue_[write_index];
        if (chunk.data.size() < byte_count) {
            auto resize_result = chunk.data.resize(byte_count, "audio_chunk_grow");
            if (!resize_result) {
                audio_drop_count_++;
                if (!audio_queue_drop_logged_) {
                    BROOKESIA_LOGW("Failed to grow NES audio queue chunk: %1%", resize_result.error());
                    audio_queue_drop_logged_ = true;
                }
                return;
            }
        }
        std::memcpy(chunk.data.data(), nes_->apu->buffer, byte_count);
        chunk.size = byte_count;
        audio_queue_count_++;
    }

    void release_display()
    {
        if (!visual_operation_) {
            return;
        }
        visual_operation_->close();
        visual_operation_.reset();
        output_name_.clear();
        output_is_buffer_ = false;
    }

    std::expected<void, std::string> setup_present_buffers(size_t buffer_size)
    {
        if (buffer_size == 0) {
            return std::unexpected("NES present buffer size is zero");
        }

        for (size_t i = 0; i < present_buffers_.size(); ++i) {
            auto label = "present_buffer[" + std::to_string(i) + "]";
            auto resize_result = present_buffers_[i].data.resize(buffer_size, label);
            if (!resize_result) {
                return resize_result;
            }
            present_buffers_[i].busy = false;
        }
        present_error_logged_ = false;
        display_backpressure_pending_ = false;
        display_backpressure_count_ = 0;
        present_drop_count_ = 0;
        present_error_count_ = 0;
        return {};
    }

    void clear_present_buffers()
    {
        std::lock_guard lock(present_mutex_);
        for (auto &buffer : present_buffers_) {
            buffer.data.clear();
            buffer.busy = false;
        }
        display_backpressure_pending_ = false;
        display_backpressure_count_ = 0;
        present_drop_count_ = 0;
        present_error_count_ = 0;
        present_cv_.notify_all();
    }

    std::optional<size_t> reserve_present_buffer(size_t buffer_size)
    {
        std::lock_guard lock(present_mutex_);
        for (size_t i = 0; i < present_buffers_.size(); ++i) {
            auto &buffer = present_buffers_[i];
            if (buffer.busy) {
                continue;
            }
            if (buffer.data.size() != buffer_size) {
                auto label = "present_buffer[" + std::to_string(i) + "]";
                auto resize_result = buffer.data.resize(buffer_size, label);
                if (!resize_result) {
                    BROOKESIA_LOGW("Failed to resize NES present buffer: %1%", resize_result.error());
                    mark_display_backpressure_locked();
                    return std::nullopt;
                }
            }
            buffer.busy = true;
            return i;
        }
        mark_display_backpressure_locked();
        return std::nullopt;
    }

    void release_present_buffer(size_t buffer_index, dataflow::VisualPresentResult result)
    {
        if (buffer_index >= present_buffers_.size()) {
            return;
        }

        bool should_log_error = false;
        {
            std::lock_guard lock(present_mutex_);
            present_buffers_[buffer_index].busy = false;
            if (result == dataflow::VisualPresentResult::DroppedQueueFull) {
                present_drop_count_++;
                mark_display_backpressure_locked();
            } else if (result == dataflow::VisualPresentResult::Error) {
                present_error_count_++;
                mark_display_backpressure_locked();
            }
            should_log_error = (result == dataflow::VisualPresentResult::Error) && !present_error_logged_;
            if (should_log_error) {
                present_error_logged_ = true;
            }
        }
        present_cv_.notify_all();
        if (should_log_error) {
            BROOKESIA_LOGW("NES Display async present failed; future errors will be suppressed");
        }
    }

    bool wait_for_present_buffers_idle()
    {
        std::unique_lock lock(present_mutex_);
        const auto timeout = std::chrono::milliseconds(
                                 static_cast<int>(BROOKESIA_EMULATION_NES_DRAW_TIMEOUT_MS) +
                                 NES_PRESENT_DRAIN_EXTRA_TIMEOUT_MS
                             );
        return present_cv_.wait_for(lock, timeout, [this]() {
            return std::none_of(present_buffers_.begin(), present_buffers_.end(), [](const auto & buffer) {
                return buffer.busy;
            });
        });
    }

    void mark_display_backpressure_locked()
    {
        display_backpressure_pending_ = true;
        display_backpressure_count_++;
    }

    std::expected<void, std::string> resolve_render_area()
    {
        const auto &area = config_.video_area;
        if ((area.x >= output_width_) || (area.y >= output_height_)) {
            return std::unexpected("NES video area origin is outside Display output");
        }

        const uint32_t max_width = output_width_ - area.x;
        const uint32_t max_height = output_height_ - area.y;
        const uint32_t viewport_width = (area.width == 0) ? max_width : area.width;
        const uint32_t viewport_height = (area.height == 0) ? max_height : area.height;
        if ((viewport_width == 0) || (viewport_height == 0) ||
                (viewport_width > max_width) || (viewport_height > max_height)) {
            return std::unexpected("NES video area is outside Display output");
        }

        uint32_t target_width = NES_NATIVE_WIDTH;
        uint32_t target_height = NES_VISIBLE_HEIGHT;
        if (config_.video_mode == VideoMode::Native) {
            if ((target_width > viewport_width) || (target_height > viewport_height)) {
                return std::unexpected("NES native frame is larger than configured video area");
            }
        } else {
            const float scale_x = static_cast<float>(viewport_width) / static_cast<float>(NES_NATIVE_WIDTH);
            const float scale_y = static_cast<float>(viewport_height) / static_cast<float>(NES_VISIBLE_HEIGHT);
            const float scale = (config_.video_mode == VideoMode::Fill) ? std::max(scale_x, scale_y) :
                                std::min(scale_x, scale_y);
            target_width = std::max<uint32_t>(1, static_cast<uint32_t>(NES_NATIVE_WIDTH * scale));
            target_height = std::max<uint32_t>(1, static_cast<uint32_t>(NES_VISIBLE_HEIGHT * scale));
            target_width = std::min(target_width, viewport_width);
            target_height = std::min(target_height, viewport_height);
        }
        render_width_ = target_width;
        render_height_ = target_height;
        output_x_ = area.x + ((viewport_width > render_width_) ? ((viewport_width - render_width_) / 2) : 0);
        output_y_ = area.y + ((viewport_height > render_height_) ? ((viewport_height - render_height_) / 2) : 0);
        auto x_map_result = x_src_map_.resize(static_cast<size_t>(render_width_) * sizeof(uint16_t), "x_src_map");
        if (!x_map_result) {
            return x_map_result;
        }
        auto row_map_result = row_src_offset_map_.resize(
                                  static_cast<size_t>(render_height_) * sizeof(uint16_t), "row_src_offset_map"
                              );
        if (!row_map_result) {
            return row_map_result;
        }
        auto *x_src_map = reinterpret_cast<uint16_t *>(x_src_map_.data());
        auto *row_src_offset_map = reinterpret_cast<uint16_t *>(row_src_offset_map_.data());
        for (uint32_t x = 0; x < render_width_; x++) {
            x_src_map[x] = static_cast<uint16_t>((x * NES_NATIVE_WIDTH) / render_width_);
        }
        for (uint32_t y = 0; y < render_height_; y++) {
            const uint32_t src_y = NES_VISIBLE_Y_OFFSET + ((y * NES_VISIBLE_HEIGHT) / render_height_);
            row_src_offset_map[y] = static_cast<uint16_t>((src_y * NES_SCREEN_PITCH) + NES_SCREEN_OVERDRAW);
        }
        BROOKESIA_LOGI(
            "NES render area: output=%1% %2%x%3%, viewport=(%4%,%5%) %6%x%7%, rect=(%8%,%9%) %10%x%11%",
            output_name_, output_width_, output_height_, area.x, area.y, viewport_width, viewport_height, output_x_,
            output_y_, render_width_, render_height_
        );
        return {};
    }

    void convert_frame_to(const uint8_t *src_vidbuf, uint8_t *dst_data, size_t dst_stride_bytes)
    {
        if ((src_vidbuf == nullptr) || (dst_data == nullptr) || (dst_stride_bytes == 0)) {
            return;
        }
        const auto *palette = reinterpret_cast<const uint16_t *>(palette_.data());
        if (palette == nullptr) {
            return;
        }
        if ((render_width_ == NES_NATIVE_WIDTH) && (render_height_ == NES_VISIBLE_HEIGHT)) {
            for (uint32_t y = 0; y < NES_VISIBLE_HEIGHT; ++y) {
                const auto *src_row = src_vidbuf + ((NES_VISIBLE_Y_OFFSET + y) * NES_SCREEN_PITCH) +
                                      NES_SCREEN_OVERDRAW;
                auto *dst_row = reinterpret_cast<uint16_t *>(dst_data + static_cast<size_t>(y) * dst_stride_bytes);
                uint32_t x = 0;
                for (; x + 7 < NES_NATIVE_WIDTH; x += 8) {
                    dst_row[x] = palette[src_row[x]];
                    dst_row[x + 1] = palette[src_row[x + 1]];
                    dst_row[x + 2] = palette[src_row[x + 2]];
                    dst_row[x + 3] = palette[src_row[x + 3]];
                    dst_row[x + 4] = palette[src_row[x + 4]];
                    dst_row[x + 5] = palette[src_row[x + 5]];
                    dst_row[x + 6] = palette[src_row[x + 6]];
                    dst_row[x + 7] = palette[src_row[x + 7]];
                }
                for (; x < NES_NATIVE_WIDTH; ++x) {
                    dst_row[x] = palette[src_row[x]];
                }
            }
            return;
        }

        const auto *x_src_map = reinterpret_cast<const uint16_t *>(x_src_map_.data());
        const auto *row_src_offset_map = reinterpret_cast<const uint16_t *>(row_src_offset_map_.data());
        if ((x_src_map == nullptr) || (row_src_offset_map == nullptr)) {
            return;
        }
        const size_t row_bytes = static_cast<size_t>(render_width_) * RGB565_BYTES_PER_PIXEL;
        uint32_t previous_src_offset = std::numeric_limits<uint32_t>::max();
        uint16_t *previous_dst_row = nullptr;
        for (uint32_t y = 0; y < render_height_; ++y) {
            const uint32_t src_offset = row_src_offset_map[y];
            auto *dst_row = reinterpret_cast<uint16_t *>(dst_data + static_cast<size_t>(y) * dst_stride_bytes);
            if ((src_offset == previous_src_offset) && (previous_dst_row != nullptr)) {
                std::memcpy(dst_row, previous_dst_row, row_bytes);
                continue;
            }

            const auto *src_row = src_vidbuf + src_offset;
            uint32_t x = 0;
            for (; x + 7 < render_width_; x += 8) {
                dst_row[x] = palette[src_row[x_src_map[x]]];
                dst_row[x + 1] = palette[src_row[x_src_map[x + 1]]];
                dst_row[x + 2] = palette[src_row[x_src_map[x + 2]]];
                dst_row[x + 3] = palette[src_row[x_src_map[x + 3]]];
                dst_row[x + 4] = palette[src_row[x_src_map[x + 4]]];
                dst_row[x + 5] = palette[src_row[x_src_map[x + 5]]];
                dst_row[x + 6] = palette[src_row[x_src_map[x + 6]]];
                dst_row[x + 7] = palette[src_row[x_src_map[x + 7]]];
            }
            for (; x < render_width_; ++x) {
                dst_row[x] = palette[src_row[x_src_map[x]]];
            }
            previous_src_offset = src_offset;
            previous_dst_row = dst_row;
        }
    }

    std::mutex runtime_mutex_;
    Config config_;
    std::shared_ptr<dataflow::VisualOperation> visual_operation_;
    std::string output_name_;
    uint32_t output_width_ = 0;
    uint32_t output_height_ = 0;
    bool output_is_buffer_ = false;
    uint32_t output_x_ = 0;
    uint32_t output_y_ = 0;
    uint32_t render_width_ = NES_NATIVE_WIDTH;
    uint32_t render_height_ = NES_VISIBLE_HEIGHT;
    std::array<PresentBuffer, NES_PRESENT_BUFFER_COUNT> present_buffers_;
    std::mutex present_mutex_;
    std::condition_variable present_cv_;
    bool present_error_logged_ = false;
    bool display_backpressure_pending_ = false;
    uint32_t display_backpressure_count_ = 0;
    uint32_t present_drop_count_ = 0;
    uint32_t present_error_count_ = 0;
    std::array<HeapBuffer, NES_VIDEO_BUFFER_COUNT> nes_vidbufs_;
    size_t nes_vidbuf_index_ = 0;
    HeapBuffer x_src_map_;
    HeapBuffer row_src_offset_map_;
    OwnedBuffer rom_buffer_;
    size_t rom_size_ = 0;
    HeapBuffer palette_;
    nes_t *nes_ = nullptr;
    std::mutex emulator_mutex_;
    std::shared_ptr<dataflow::AudioPlaybackOperation> audio_operation_;
    std::string audio_output_name_;
    bool audio_started_ = false;
    bool audio_error_logged_ = false;
    bool audio_queue_drop_logged_ = false;
    mutable std::mutex audio_queue_mutex_;
    std::vector<AudioChunk> audio_queue_;
    HeapBuffer audio_feed_buffer_;
    size_t audio_queue_head_ = 0;
    size_t audio_queue_count_ = 0;
    size_t audio_chunk_bytes_ = 0;
    uint32_t audio_drop_count_ = 0;
    std::mutex gamepad_mutex_;
    GamepadState gamepad_state_;
};


} // namespace esp_brookesia::emulation
