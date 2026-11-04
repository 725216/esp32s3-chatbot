# AV Processor V2 Component (Audio/Video Processing)

`av_processor_v2` is an application-facing audio/video processing component that organizes common media capabilities on ESP platforms into a more unified and easier-to-integrate set of modules.

It is built on top of GMF, `esp_audio_simple_player`, and related middleware. Video capture and codec stacks (`esp_capture`, `esp_video_codec`) are optional. The component is intended for projects that need audio recording, playback, optional video capture, and debugging-oriented data export.

## Platform Support

- **ESP-IDF**: v6.0 and above (tested with GCC 15 toolchain defaults)
- **Targets**: ESP32, ESP32-S2, ESP32-S3, ESP32-C3/C5/C6, ESP32-P4, **ESP32-S31**, and other chips supported by the underlying GMF/audio dependencies

## Documentation

This `README.md` is kept as a high-level overview only.

If you need full API details, configuration structures, lifecycle information, mode differences, usage flows, or implementation notes, go directly to the following documents:

- Detailed guide (includes full API index): [docs/COMPONENT_GUIDE.md](docs/COMPONENT_GUIDE.md)
- Audio API header: [include/audio_processor.h](include/audio_processor.h)
- Video API header: [include/video_processor.h](include/video_processor.h)
- Shared type definitions: [include/av_processor_type.h](include/av_processor_type.h)

## Capability Overview

### Audio

- Audio manager initialization and board-level input/output abstraction
- Audio recording
- Frontend processing on the recording path
- Encoded recorder output, including PCM, G711, OPUS, AAC, MP3, and related formats
- Unified audio playback through `audio_play_handle_t`, for URL or local file sources
- Feeder playback for RTC, network stream, or custom transport receive paths
- Prompt playback through the same `audio_play_*` API, for short, blocking, high-priority sounds
- Optional mixer support with focus and fade control between playback and feeder streams

### Frontend and Algorithm Support

- Built-in `ai_afe` path, using the complete AFE stack from `esp-sr`
- Custom frontend path, intended for composing independent `esp-sr` modules
- Optional AEC, NS, VAD, AGC, wakeup, and voice command detection capabilities
- Current custom-path support for standalone WakeNet use cases

## Kconfig Options

Configure the component under **Component config → Audio/Video Processor Configuration** in `menuconfig`, or set options in your project's `sdkconfig.defaults`.

| Option | Description |
|--------|-------------|
| `CONFIG_AUDIO_PROCESSOR_IMPL_GMF` | GMF pipeline implementation (default). `audio_manager_init()` registers default codecs and calls `media_lib_add_default_adapter()`. Pulls `media_lib_sal` and `esp_hls_stream` (when applicable) as managed dependencies. |
| `CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT` | Lightweight recorder/feeder for lower RAM usage; keeps playback, uses `audio_processor_light.c`. |
| `CONFIG_VIDEO_PROCESSOR_ENABLE` | Build video capture/render (`video_processor.c`) and pull `esp_capture` / `esp_video_codec`. Default **enabled** on ESP32-S3 and ESP32-P4; default **disabled** on other targets. Set to `n` for audio-only builds. |
| `CONFIG_AUDIO_AFE_ENABLE` | Enable AFE on the recording path. Default **enabled** on ESP32, ESP32-S3, ESP32-P4, and ESP32-S31. |
| `CONFIG_AUDIO_HLS_ENABLE` | HLS playback support (GMF only). |
| `CONFIG_MEDIA_DUMP_ENABLE` | Debug audio dump to SD card or UDP. |

**Audio-only example:** [examples/audio_afe](examples/audio_afe) sets `CONFIG_VIDEO_PROCESSOR_ENABLE=n` in `sdkconfig.defaults` to avoid pulling the video dependency chain.

**Video example:** [examples/video_preview](examples/video_preview) sets `CONFIG_VIDEO_PROCESSOR_ENABLE=y`.

After changing Kconfig options that affect managed dependencies, run `idf.py reconfigure` or `idf.py fullclean` before building.

## Lightweight Audio Path

For lower-memory targets, the component also provides `CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT`, which builds [`src/audio_processor_light.c`](src/audio_processor_light.c).

This variant keeps `audio_manager_init()` and the existing playback path, but replaces recorder and feeder with a simpler single-task queue-based implementation focused on reducing internal RAM usage.

It is intended for chips without PSRAM, such as ESP32-C3. The recommended starting example is [examples/audio_wn_play_rec](examples/audio_wn_play_rec), which combines wake word detection, recording, feeder playback, and flash loopback in one low-memory flow.

## Audio Instance Model

The current audio API uses explicit handles:

- `audio_recorder_open(..., &handle)` creates a recorder instance
- `audio_feeder_open(..., &handle)` creates a feeder instance
- `audio_play_open(..., &handle)` creates a play instance
- `audio_play_config_t.type` distinguishes normal playback from prompt playback

Follow-up operations such as `pause`, `resume`, `read`, `feed`, `play`, `stop`, and `close` all take the corresponding handle explicitly.

That means one application can hold multiple instances at the same time, for example multiple recorders or both playback-type and prompt-type play instances.

### Video (optional, `CONFIG_VIDEO_PROCESSOR_ENABLE`)

When video is enabled, the component provides:

- Video capture
- Multi-sink output
- Stream-mode callback handling
- Fetch-mode frame acquire/release flow
- Video decoding
- Render callbacks for decoded frames

### Debugging and Analysis

- Media Dump support for debugging
- Export of raw audio data from the recorder path to SD card or UDP
- Capture points before or after AEC for echo, feedback, clipping, noise floor, and processing-effect analysis

## Typical Use Cases

- Device-side audio recording for upload or local storage
- Receiving network audio and playing it locally while decoding on the fly
- Inserting high-priority prompt sounds while a main playback stream is active
- Capturing camera frames for local preview, encoded output, or multi-path distribution
- Outputting both compressed and raw video from the same source path
- Reproducing field issues and exporting raw audio for offline debugging

## Example Applications

This repository already includes several representative examples. In most cases, the fastest way to understand how the component is intended to be used is to start from one of them.

### Audio Examples

- [examples/audio_afe](examples/audio_afe)
  Demonstrates the built-in `ai_afe` path on ESP32-S31 (and other targets). Audio-only configuration (`CONFIG_VIDEO_PROCESSOR_ENABLE=n`). Best starting point for AEC, NS, VAD, and AGC integration.

- [examples/audio_sr_vc_switch](examples/audio_sr_vc_switch)
  Demonstrates switching between `AFE_TYPE_SR` and `AFE_TYPE_VC` recorder frontends, and is also useful for understanding multiple recorder instances and console-driven switching flow.

- [examples/audio_wn](examples/audio_wn)
  Demonstrates the custom frontend path and standalone WakeNet, and is also useful for understanding recorder event callbacks.

- [examples/audio_wn_play_rec](examples/audio_wn_play_rec)
  Demonstrates the lightweight `audio_processor_light.c` path for wake word, recording, and feeder playback on no-PSRAM targets such as ESP32-C3.

- [examples/audio_echo](examples/audio_echo)
  Demonstrates playback- and feeder-related flows, including receive-then-play style audio pipelines.

- [examples/audio_play_test](examples/audio_play_test)
  Demonstrates the unified `audio_play_*` API, normal playback and prompt playback, feeder input, mixer focus switching, and console interaction commands.

- [examples/audio_mem_leak](examples/audio_mem_leak)
  Used to repeatedly validate open/close cycles for recorder, play, feeder, and related modules, and is helpful for memory-footprint and leak investigation.

### Video Examples

Requires `CONFIG_VIDEO_PROCESSOR_ENABLE=y`.

- [examples/video_preview](examples/video_preview)
  Demonstrates basic video capture and preview flow and is the easiest entry point for capture usage.

- [examples/video_test](examples/video_test)
  Used for validating and testing the video pipeline, and is useful when checking sink configuration, format handling, and processing behavior.

## Media Dump

Media Dump is a debugging-oriented helper capability rather than a normal application-facing API.

At the moment it is primarily used to export audio data from the recorder path. Typical reasons to enable it include:

- Investigating playback-to-microphone feedback or echo issues
- Comparing audio before and after AEC processing
- Checking clipping, saturation, elevated noise floor, or format-related problems

Current support includes:

- Capture before AEC
- Capture after AEC
- SD card file output
- UDP output

For detailed configuration steps and behavior notes, see:

- Media Dump details: [docs/COMPONENT_GUIDE.md](docs/COMPONENT_GUIDE.md)

## Reading Path

- If you only want to know what this component does and which example to start from, this README is enough.
- If you are integrating the component and need structure definitions, macros, call order, the full API index, or mode differences, go to [docs/COMPONENT_GUIDE.md](docs/COMPONENT_GUIDE.md).
- If you need exact function signatures, enums, or configuration fields, go straight to the public headers:
  [include/audio_processor.h](include/audio_processor.h),
  [include/video_processor.h](include/video_processor.h),
  [include/av_processor_type.h](include/av_processor_type.h)

## Version

Current version: `v0.6.6`

## License

This component follows the MIT license. See the `LICENSE` file in the repository for details.

## Related Resources

- Issue reporting: https://github.com/espressif/esp-gmf/issues
