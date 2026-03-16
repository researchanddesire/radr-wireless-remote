#ifndef DISPLAY_UTILS_H
#define DISPLAY_UTILS_H

#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <algorithm>
#include <constants/Colors.h>
#include <constants/Sizes.h>
#include <qrcode.h>

void drawScrollBar(int currentOption, int numOptions);
void clearPage(bool clearStatusbar = false);

struct DrawQRCodeProps {
    int x = Display::WIDTH;
    int y = Display::PageY;

    int maxWidth = Display::WIDTH;
    int maxHeight = Display::HEIGHT;
};

int drawQRCode(Adafruit_GFX &gfx, const String &qrValue,
               const DrawQRCodeProps &props);

struct WrapTextProps {
    int x = 0;
    int y = 8;
    int rightPadding = 0;
};

void wrapText(Adafruit_GFX &gfx, const String &text,
              const WrapTextProps &props);

#endif
