#ifndef LOCKBOX_CAPTURED_DISPLAY_H
#define LOCKBOX_CAPTURED_DISPLAY_H

#ifdef VERSIONDEV
#include <Adafruit_ST7789.h>

// Mirror exactly the logical RGB565 writes sent to the write-only TFT. This
// development-only buffer supports USB inspection without changing UI layouts.
class CapturedDisplay : public Adafruit_ST7789 {
public:
    using Adafruit_ST7789::Adafruit_ST7789;
    void initCapture();
    using Adafruit_ST7789::drawRGBBitmap;
    void drawRGBBitmap(int16_t x, int16_t y, uint16_t* pixels, int16_t w, int16_t h);
    void captureBitmap(int x, int y, const uint16_t* pixels, int w, int h);
    uint16_t* copyCapture(); // Caller holds displayMutex; caller frees result.

    void drawPixel(int16_t x, int16_t y, uint16_t c) override;
    void writePixel(int16_t x, int16_t y, uint16_t c) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override;
    void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override;
    void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override;
    void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override;
private:
    uint16_t* pixels_ = nullptr;
    void captureRect(int x, int y, int w, int h, uint16_t color);
};

void startScreenCaptureConsole();
#endif
#endif
