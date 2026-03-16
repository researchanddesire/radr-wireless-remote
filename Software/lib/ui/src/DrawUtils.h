#ifndef UI_DRAW_UTILS_H
#define UI_DRAW_UTILS_H

#include <Adafruit_GFX.h>

#include <string>

#include "DisplayConstants.h"

namespace ui {

void clearPage(Adafruit_GFX &gfx, bool clearStatusbar = false);
void clearScreen(Adafruit_GFX &gfx);

struct DrawQRCodeProps {
    int x = Display::WIDTH;
    int y = Display::PageY;
    int maxWidth = Display::WIDTH;
    int maxHeight = Display::HEIGHT;
};

int drawQRCode(Adafruit_GFX &gfx, const std::string &qrValue,
               const DrawQRCodeProps &props = DrawQRCodeProps());

struct WrapTextProps {
    int x = 0;
    int y = 8;
    int rightPadding = 0;
};

void wrapText(Adafruit_GFX &gfx, const std::string &text,
              const WrapTextProps &props = WrapTextProps());

void drawScrollBar(Adafruit_GFX &gfx, int currentOption, int numOptions);

}  // namespace ui

#endif  // UI_DRAW_UTILS_H
