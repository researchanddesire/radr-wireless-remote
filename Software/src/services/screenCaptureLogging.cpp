#ifdef VERSIONDEV
#include "screenCaptureLogging.h"
#include <Arduino.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <esp_arduino_version.h>
#include <esp_log.h>

namespace {
std::atomic<bool> serialReady{false};

// Use the same Serial.write mutex as framed rows, as the staging health
// observer does. Keep every ordinary/fault log; hold no display mutex here.
int serialLog(const char* format, va_list arguments) {
    char local[256];
    va_list copy;
    va_copy(copy, arguments);
    const int count = vsnprintf(local, sizeof(local), format, copy);
    va_end(copy);
    if (count < 0) return count;
    if (static_cast<size_t>(count) < sizeof(local))
        return Serial.write(reinterpret_cast<const uint8_t*>(local), count);
    auto* expanded = static_cast<char*>(malloc(static_cast<size_t>(count) + 1));
    if (!expanded) return vprintf(format, arguments); // Preserve the log on OOM.
    vsnprintf(expanded, static_cast<size_t>(count) + 1, format, arguments);
    const int written = Serial.write(reinterpret_cast<const uint8_t*>(expanded), count);
    free(expanded);
    return written;
}
}

#if ESP_ARDUINO_VERSION_MAJOR == 2
extern "C" int log_printfv(const char* format, va_list arguments);
// Source-built Arduino 2 uses --wrap=log_printf. The prebuilt ESP32 SDK owns a
// strong diagnostics wrapper, which must retain precedence over this one.
extern "C" __attribute__((weak)) int __wrap_log_printf(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = serialReady.load() ? serialLog(format, arguments)
                                          : log_printfv(format, arguments);
    va_end(arguments);
    return result;
}
#endif

void startScreenCaptureLogging() {
    serialReady.store(true);
    esp_log_set_vprintf(serialLog);
}
#endif
