#pragma once

// Development-only, read-only USB console. Keep the wire format compatible
// with Lockbox's original LKBX_SCREEN_* records (RGB565, little endian FNV-1a).
#ifdef VERSIONDEV
#include <Arduino.h>
#include <cstdlib>
#include <cstring>
#include "screenCaptureLogging.h"

namespace screenCapture {
struct Source {
    const char* product;
    int width;
    int height;
    void* (*copy)(const char*& error); // Owns display lock only while copying.
    uint16_t (*pixel)(const void* snapshot, int x, int y);
};

inline void error(const Source& source, const char* reason) {
    Serial.printf("\n%s_SCREEN_ERROR %s\n", source.product, reason);
}

inline void dump(const Source& source) {
    const char* reason = "unavailable";
    void* snapshot = source.copy(reason);
    if (!snapshot) { error(source, reason); return; }
    const uint32_t id = millis();
    uint32_t hash = 2166136261u;
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            const uint16_t color = source.pixel(snapshot, x, y);
            hash = (hash ^ (color & 255u)) * 16777619u;
            hash = (hash ^ (color >> 8)) * 16777619u;
        }
    }
    Serial.printf("\n%s_SCREEN_BEGIN %lu %d %d %08lx\n", source.product,
                  static_cast<unsigned long>(id), source.width, source.height,
                  static_cast<unsigned long>(hash));
    // One write per row; unrelated/interleaved logs are rejected by the reader.
    // The display lock has already been released, so slow USB cannot freeze UI.
    char line[320 * 7 + 80];
    bool complete = true;
    for (int y = 0; y < source.height; ++y) {
        int used = snprintf(line, sizeof(line), "%s_SCREEN_ROW %lu %d ",
                            source.product, static_cast<unsigned long>(id), y);
        for (int x = 0; x < source.width;) {
            const uint16_t color = source.pixel(snapshot, x, y);
            int count = 1;
            while (x + count < source.width &&
                   source.pixel(snapshot, x + count, y) == color) ++count;
            used += snprintf(line + used, sizeof(line) - used, "%03x%04x", count, color);
            x += count;
        }
        line[used++] = '\n';
        if (Serial.write(reinterpret_cast<const uint8_t*>(line), used) != size_t(used)) {
            complete = false;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    free(snapshot);
    if (complete)
        Serial.printf("%s_SCREEN_END %lu\n", source.product, static_cast<unsigned long>(id));
    else
        error(source, "serial_write");
}

inline void console(void* arg) {
    const auto& source = *static_cast<const Source*>(arg);
    char command[16]{};
    size_t used = 0;
    bool overflow = false;
    for (;;) {
        while (Serial.available()) {
            const int c = Serial.read();
            if (c < 0) break;
            if (c == '\n') {
                command[used] = '\0';
                if (!overflow && strcmp(command, "screen") == 0) dump(source);
                used = 0;
                overflow = false;
            } else if (c != '\r' && !overflow) {
                if (used < sizeof(command) - 1) command[used++] = char(c);
                else overflow = true; // Discard the whole overlong command.
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

inline void start(const Source& source) {
    startScreenCaptureLogging();
    // Source must have static lifetime; the current products are 128x64/320x240.
    if (source.width < 1 || source.width > 320 || source.height < 1 || source.height > 240) {
        error(source, "dimensions");
        return;
    }
    if (xTaskCreate(console, "screenCapture", 6144,
                    const_cast<Source*>(&source), 1, nullptr) != pdPASS)
        error(source, "console_task");
}
} // namespace screenCapture
#endif
