#ifndef UI_H
#define UI_H

#include "DisplayConstants.h"
#include "DisplayTypes.h"
#include "DrawUtils.h"
#include "Icons.h"
#include "Strings.h"
#include "TextPages.h"

namespace ui {

void drawHeaderBar(Adafruit_GFX &gfx, const HeaderBarData &data);
void drawTextPage(Adafruit_GFX &gfx, const TextPage &page,
                  const HeaderBarData &header = HeaderBarData());
void drawButton(Adafruit_GFX &gfx, const std::string &text, int16_t x,
                int16_t y, int16_t w = 70, int16_t h = 35,
                bool pressed = false,
                uint16_t bgColor = Colors::textBackground,
                uint16_t textColor = Colors::black);
void drawMenu(Adafruit_GFX &gfx, const MenuData &data);
void drawMenuItem(Adafruit_GFX &gfx, int index, const MenuItem &option,
                  bool selected, int &yOffset, int menuWidth);

}  // namespace ui

#endif  // UI_H
