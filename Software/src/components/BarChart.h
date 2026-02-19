#ifndef BARCHART_H
#define BARCHART_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST77xx.h>
#include <Fonts/FreeSans9pt7b.h>
#include "DisplayObject.h"
#include "constants/Colors.h"
#include "services/display.h"

extern Adafruit_ST7789 tft;
extern SemaphoreHandle_t displayMutex;

class BarChart : public DisplayObject {
  private:
    const char *label;
    float *value;
    int minValue;
    int maxValue;
    bool focused;
    int lastDrawnValue = -9999;
    bool lastFocusedState = false;
    uint16_t barColor;

    static constexpr int LABEL_WIDTH = 80;
    static constexpr int VALUE_WIDTH = 40;
    static constexpr int BAR_MARGIN = 4;
    static constexpr int CORNER_RADIUS = 3;

  public:
    BarChart(const char *label, float *value, int minValue, int maxValue,
             int16_t x, int16_t y, int16_t w, int16_t h,
             bool focused = false, uint16_t barColor = Colors::sensation)
        : DisplayObject(x, y, w, h),
          label(label),
          value(value),
          minValue(minValue),
          maxValue(maxValue),
          focused(focused),
          barColor(barColor) {}

    void setFocused(bool f) {
        if (focused != f) {
            focused = f;
            isDirty = true;
        }
    }

    void setBarColor(uint16_t color) {
        if (barColor != color) {
            barColor = color;
            isDirty = true;
        }
    }

    bool shouldDraw() override {
        if (value == nullptr) return false;
        int currentValue = constrain((int)roundf(*value), minValue, maxValue);
        if (currentValue != lastDrawnValue) return true;
        if (focused != lastFocusedState) return true;
        return false;
    }

    void draw() override {
        if (value == nullptr) return;
        int currentValue = constrain((int)roundf(*value), minValue, maxValue);

        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

        bool fullRedraw = isFirstDraw || (focused != lastFocusedState);

        if (fullRedraw) {
            tft.fillRect(x, y, width, height, ST77XX_BLACK);
        }

        uint16_t fgColor = focused ? barColor : Colors::bgGray600;
        uint16_t textColor = focused ? Colors::textForeground
                                     : Colors::textForegroundSecondary;

        // Label on the left
        tft.setFont(nullptr);
        tft.setTextColor(textColor, ST77XX_BLACK);
        tft.setCursor(x + 2, y + (height / 2) - 3);
        if (fullRedraw) {
            tft.fillRect(x, y, LABEL_WIDTH, height, ST77XX_BLACK);
        }
        tft.print(label);

        // Bar region
        int barX = x + LABEL_WIDTH;
        int barW = width - LABEL_WIDTH - VALUE_WIDTH;
        int barInnerW = barW - 2 * BAR_MARGIN;
        int barY = y + BAR_MARGIN;
        int barH = height - 2 * BAR_MARGIN;

        int range = maxValue - minValue;
        int fillW = 0;
        if (range > 0) {
            fillW = (int)((long)(currentValue - minValue) * barInnerW / range);
        }
        fillW = constrain(fillW, 0, barInnerW);

        if (fullRedraw) {
            tft.fillRect(barX, y, barW, height, ST77XX_BLACK);
            tft.drawRoundRect(barX + BAR_MARGIN, barY, barInnerW, barH,
                              CORNER_RADIUS, fgColor);
        }

        // Differential fill: only redraw bar interior when value changes
        int lastFillW = 0;
        if (range > 0 && lastDrawnValue >= minValue) {
            lastFillW = (int)((long)(lastDrawnValue - minValue) * barInnerW / range);
            lastFillW = constrain(lastFillW, 0, barInnerW);
        }

        int innerX = barX + BAR_MARGIN + 1;
        int innerY = barY + 1;
        int innerH = barH - 2;

        if (fullRedraw || fillW != lastFillW) {
            if (fullRedraw || fillW < lastFillW) {
                tft.fillRect(innerX, innerY, barInnerW - 2, innerH, ST77XX_BLACK);
            } else if (fillW > lastFillW) {
                tft.fillRect(innerX + lastFillW, innerY,
                             fillW - lastFillW, innerH, fgColor);
            }
            if (fillW > 0) {
                tft.fillRect(innerX, innerY, fillW - 1, innerH, fgColor);
            }
        }

        // Value readout on the right
        int valX = x + width - VALUE_WIDTH;
        tft.setFont(nullptr);
        tft.setTextColor(textColor, ST77XX_BLACK);
        if (fullRedraw || currentValue != lastDrawnValue) {
            tft.fillRect(valX, y, VALUE_WIDTH, height, ST77XX_BLACK);
            String valStr = String(currentValue);
            tft.setCursor(valX + 4, y + (height / 2) - 3);
            tft.print(valStr);
        }

        xSemaphoreGive(displayMutex);

        lastDrawnValue = currentValue;
        lastFocusedState = focused;
    }
};

#endif
