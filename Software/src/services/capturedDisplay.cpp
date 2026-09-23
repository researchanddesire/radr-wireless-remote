#ifdef VERSIONDEV
#include "capturedDisplay.h"
#include "display.h"
#include "screenCaptureProtocol.h"
#include <esp_heap_caps.h>
#include <cstring>

namespace {
constexpr int kCaptureWidth = 320;
constexpr int kCaptureHeight = 240;
constexpr size_t kCaptureBytes = kCaptureWidth * kCaptureHeight * sizeof(uint16_t);
uint16_t* allocatePixels() {
    return static_cast<uint16_t*>(heap_caps_malloc(kCaptureBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
}

void CapturedDisplay::initCapture() {
    pixels_ = allocatePixels();
    if (pixels_) memset(pixels_, 0, kCaptureBytes);
}

void CapturedDisplay::captureRect(int x, int y, int w, int h, uint16_t c) {
    if (!pixels_ || !w || !h) return;
    if (w < 0) { x += w + 1; w = -w; }
    if (h < 0) { y += h + 1; h = -h; }
    const int right = min(kCaptureWidth, x + w);
    const int bottom = min(kCaptureHeight, y + h);
    for (int row = max(0, y); row < bottom; ++row)
        for (int col = max(0, x); col < right; ++col)
            pixels_[row * kCaptureWidth + col] = c;
}

void CapturedDisplay::captureBitmap(int x, int y, const uint16_t* src, int w, int h) {
    if (!pixels_ || !src) return;
    for (int row = max(0, y); row < min(kCaptureHeight, y + h); ++row)
        for (int col = max(0, x); col < min(kCaptureWidth, x + w); ++col)
            pixels_[row * kCaptureWidth + col] = src[(row - y) * w + col - x];
}

uint16_t* CapturedDisplay::copyCapture() {
    if (!pixels_) return nullptr;
    auto* result = allocatePixels();
    if (result) memcpy(result, pixels_, kCaptureBytes);
    return result;
}

void CapturedDisplay::drawPixel(int16_t x, int16_t y, uint16_t c) {
    captureRect(x, y, 1, 1, c);
    Adafruit_ST7789::drawPixel(x, y, c);
}
void CapturedDisplay::writePixel(int16_t x, int16_t y, uint16_t c) {
    captureRect(x, y, 1, 1, c);
    Adafruit_ST7789::writePixel(x, y, c);
}
void CapturedDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    captureRect(x, y, w, h, c);
    Adafruit_ST7789::fillRect(x, y, w, h, c);
}
void CapturedDisplay::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    captureRect(x, y, w, h, c);
    Adafruit_ST7789::writeFillRect(x, y, w, h, c);
}
void CapturedDisplay::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
    captureRect(x, y, w, 1, c);
    Adafruit_ST7789::drawFastHLine(x, y, w, c);
}
void CapturedDisplay::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) {
    captureRect(x, y, 1, h, c);
    Adafruit_ST7789::drawFastVLine(x, y, h, c);
}
void CapturedDisplay::writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
    captureRect(x, y, w, 1, c);
    Adafruit_ST7789::writeFastHLine(x, y, w, c);
}
void CapturedDisplay::writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) {
    captureRect(x, y, 1, h, c);
    Adafruit_ST7789::writeFastVLine(x, y, h, c);
}

void CapturedDisplay::drawRGBBitmap(int16_t x, int16_t y, uint16_t* src,
                                    int16_t w, int16_t h) {
    captureBitmap(x, y, src, w, h);
    Adafruit_ST7789::drawRGBBitmap(x, y, src, w, h);
}

namespace {
void* copyScreen(const char*& error) {
    if (!displayMutex || xSemaphoreTake(displayMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        error = "busy";
        return nullptr;
    }
    auto* snapshot = tft.copyCapture();
    xSemaphoreGive(displayMutex);
    if (!snapshot) error = "no_memory";
    return snapshot;
}
uint16_t pixel(const void* snapshot, int x, int y) {
    return static_cast<const uint16_t*>(snapshot)[y * kCaptureWidth + x];
}
const screenCapture::Source source{ "RADR", kCaptureWidth, kCaptureHeight, copyScreen, pixel };
}
void startScreenCaptureConsole() { screenCapture::start(source); }
#endif
