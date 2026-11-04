# Changelog

## v0.6.6

### Documentation

- Update README and README_CN (platform support, Kconfig table, optional video and GMF notes); version bump to v0.6.6

## v0.6.5

### Feature

- Support ESP-IDF v6.0 and above
- Support ESP32-S31

## v0.6.4

### Feature

- Add `audio_processor_light.c` as a lightweight recorder/feeder implementation selectable through `CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT`
- Add `examples/audio_wn_play_rec` for wake word, recording, and feeder playback on no-PSRAM targets such as ESP32-C3
- Update README and component guides to document the lightweight implementation and its recommended example

## v0.6.3

### Bugfix

- Fixed HLS dependency issue

## v0.6.2

### Feature

- Support HLS playback for audio

## v0.6.1

### Feature

- Add `examples/audio_play_eq` example for SD card playback with post-player EQ processing

## v0.6.0

### Feature

- Refactored recorder, feeder, and playback to be instantiated during creation, allowing multiple instances
- Integrated prompt functionality into playback, with `audio_play_type_t` used to distinguish playback types
- Refactored audio input/output to use user-provided callbacks instead of relying on esp_codec_dev
- Recorder now supports both AFE and custom modes; custom mode allows user-defined implementations and supports C-series chips
- Renamed `audio_feeder_run` to `audio_feeder_start` for consistency
- Enhanced `audio_mixer_open` by adding parameters to bind specific playbac

## v0.5.8

### Feature

- Update gmf version to v0.8

## v0.5.7

### Feature

- Revert esp32s3 dvp camera init

## v0.5.6

### Feature

- Video render support stream mode
- Add `video_render_get_image_resolution` API

### Bugfix

- Fixed a issue when called `audio_feeder_stop`

## v0.5.5

### Feature

- Support MP3 format to feed
- support esp-capture v0.7.9

## v0.5.5

### Feature

- Support esp_board_manager version greater than or equal to 0.5
- Add `video_capture_fetch_frame_acquire` and `video_capture_fetch_frame_release` APIs to support video capture stream and fetch modes.

## v0.5.4

### Bugfix

- Fix API redefintion issue

## v0.5.3

### Feature

- Add `audio_recorder_get_afe_element_handle` API

## v0.5.2

### Bugfix

- Fix `feeder` and `audio prompt` playback conflict issue

## v0.5.1

### Bugfix

- Fix incorrect duration calculation when reading G711 encoded data.

## v0.5.0

### Features

- Initial version of `av_processor`
