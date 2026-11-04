/*
 * esp_log shim for esp_peer (esp-webrtc prebuilt library).
 *
 * IDF 5.3's log component provides esp_log_timestamp() but not the bare
 * `esp_log()` symbol referenced by esp-webrtc's prebuilt libpeer_default.a.
 * This file provides that symbol (printf-style logging to IDF log).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *WEBRTC_TAG = "WEBRTC";

int esp_log(const char *format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (ret > 0) {
        /* Trim trailing newline to avoid double line breaks from ESP_LOGW */
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        ESP_LOGW(WEBRTC_TAG, "%s", buffer);
    }
    return ret;
}
