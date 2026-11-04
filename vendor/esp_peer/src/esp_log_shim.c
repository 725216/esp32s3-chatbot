/*
 * esp_log shim for esp_peer (esp-webrtc prebuilt library).
 *
 * IDF 5.3's log component provides esp_log_timestamp() but not the bare
 * `esp_log()` symbol referenced by esp-webrtc's prebuilt libpeer_default.a
 * (and by esp_audio_codec prebuilt libs). This file provides that symbol.
 *
 * LOCAL FIX (2026-10-28): the prebuilt libs call `esp_log` with the legacy
 * (tag, fmt, ...) ABI while this shim only receives the first argument, so
 * forwarding to vprintf/esp_log_writev produced a NULL-pointer
 * LoadProhibited panic in newlib (_vfprintf_r, EXCVADDR=0x2). Log output
 * from these prebuilt libs is diagnostic-only, so make the shim a safe
 * no-op to keep the audio/webrtc pipelines alive.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

int esp_log(const char *format, ...)
{
    (void)format;
    return 0;
}
