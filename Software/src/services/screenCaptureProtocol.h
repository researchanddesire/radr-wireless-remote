#pragma once

// Development-only, read-only USB console. Keep the wire format compatible
// with Lockbox's original LKBX_SCREEN_* records (RGB565, little endian FNV-1a).
#ifdef VERSIONDEV
#include <Arduino.h>
#include <cstdlib>
#include <cstring>
#include "screenCaptureLogging.h"
#include "serialIdentity.h"

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

inline void transmit(const Source& source, const void* snapshot, uint32_t id) {
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
        // TFT rows can exceed 2 KiB. Let native USB and the host drain each
        // row before another burst; capture runs independently of UI rendering.
        vTaskDelay(pdMS_TO_TICKS(source.width > 128 ? 50 : 1));
    }
    if (complete)
        Serial.printf("%s_SCREEN_END %lu\n", source.product, static_cast<unsigned long>(id));
    else
        error(source, "serial_write");
}

inline const Source* activeSource = nullptr;
inline void* snapshot = nullptr;
inline uint32_t snapshotId = 0;
inline uint32_t lastRequest = 0;

inline void command(const char* text) {
    const auto& source = *activeSource;
    if (strcmp(text, "screen") == 0) {
        free(snapshot);
        const char* reason = "unavailable";
        snapshot = source.copy(reason);
        snapshotId = millis();
        if (snapshot) transmit(source, snapshot, snapshotId);
        else error(source, reason);
        lastRequest = millis();
    } else if (strcmp(text, "screen retry") == 0) {
        if (snapshot) transmit(source, snapshot, snapshotId);
        else error(source, "no_snapshot");
        lastRequest = millis();
    } else if (strcmp(text, "screen release") == 0) {
        free(snapshot);
        snapshot = nullptr;
    }
}

inline void poll() {
    if (snapshot && millis() - lastRequest >= 90000) {
        free(snapshot);
        snapshot = nullptr;
    }
}

inline void start(const Source& source) {
    startScreenCaptureLogging();
    // Source must have static lifetime; the current products are 128x64/320x240.
    if (source.width < 1 || source.width > 320 || source.height < 1 || source.height > 240) {
        error(source, "dimensions");
        return;
    }
    activeSource = &source;
    setSerialDevelopmentHandler(command, poll);
}
} // namespace screenCapture
#endif
