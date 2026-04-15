#ifndef ADVANCED_MODIFIER_CHART_H
#define ADVANCED_MODIFIER_CHART_H

#include <Arduino.h>

#include <Adafruit_MCP23X17.h>
#include <Adafruit_ST77xx.h>
#include <components/DisplayObject.h>

#include "esp_log.h"
#include "services/display.h"
#include "services/leds.h"
// Adafruit GFX fonts
#include <AiEsp32RotaryEncoder.h>
#include <vector>

#include "AdvancedStructs.h"

class AdvancedModifierChart : public DisplayObject {
  private:
    int lastValue;
    AiEsp32RotaryEncoder &encoder;

  public:
    struct Props {
        AiEsp32RotaryEncoder *encoder = nullptr;
        int16_t x = 10;
        int16_t y = 60;
        int16_t width = 300;
        int16_t height = 150;
    };

    explicit AdvancedModifierChart(const Props &props) : DisplayObject(props.x, props.y, props.width, props.height), encoder(*props.encoder) {}

    bool shouldDraw() override {
        int currentValue = encoder.readEncoder();
        if (lastValue != currentValue) {
            lastValue = currentValue;
            return true;
        }
        return false;
    }

    void draw() override {
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            tft.fillRect(x - 1, y - 1, width + 2, height + 2, COLOR_BLACK);
            uint8_t maxSteps = 4;
            for (int c = 0; c < controlNames.size(); c++) {
                uint8_t modSteps = 0;
                for (int m = 1; m < controlNames.size() - 1; m++) {
                    modSteps += advancedSettings[controlNames[c] + modifierNames[m]].value;
                }
                maxSteps = max(maxSteps, modSteps);
            }
            float stepWidth = width / float(maxSteps);
            for (int c = 0; c < controlNames.size(); c++) {
                uint16_t lineColor = advancedColors[c];
                Control control = advancedSettings[controlNames[c]];

                float baseValueRatio = (1 - control.value / 100.0);
                float modValueRatio = (1 - advancedSettings[controlNames[c] + modifierNames[0]].value / 100.0);
                float strokeRatio = 1 - baseValueRatio;
                if (c < 2) {
                    strokeRatio = (advancedSettings[controlNames[0]].value - advancedSettings[controlNames[1]].value) / 100.0;
                }
                uint16_t baseY = height * baseValueRatio + y;
                uint16_t modY = baseY + height * strokeRatio * modValueRatio;
                if (c == 1) {
                    modY = baseY - height * strokeRatio * modValueRatio;
                }
                int startX = x - stepWidth * advancedSettings[controlNames[c] + modifierNames[5]].value;
                int m = 0;
                while (startX < x + width) {
                    uint16_t step = advancedSettings[controlNames[c] + modifierNames[m + 1]].value * stepWidth;
                    switch (m) {
                        case 0:
                            tft.drawLine(startX, baseY, startX + step, modY, lineColor);
                            break;
                        case 1:
                            tft.drawLine(startX, modY, startX + step, modY, lineColor);
                            break;
                        case 2:
                            tft.drawLine(startX, modY, startX + step, baseY, lineColor);
                            break;
                        case 3:
                            tft.drawLine(startX, baseY, startX + step, baseY, lineColor);
                            break;
                    }

                    startX += step;
                    m = (m + 1) % 4;
                }
            }
            // tft.drawRoundRect(x - 1, y - 1, width + 2, height + 2, 5, COLOR_WHITE);
        }
        xSemaphoreGive(displayMutex);
    }
};

#endif