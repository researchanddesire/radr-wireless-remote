#ifndef ENCODERBAR_H
#define ENCODERBAR_H

#include <Arduino.h>

#include <Adafruit_MCP23X17.h>
#include <Adafruit_ST77xx.h>

#include "DisplayObject.h"
#include "esp_log.h"
#include "services/display.h"
#include "services/leds.h"
// Adafruit GFX fonts
#include <AiEsp32RotaryEncoder.h>
#include <vector>

class EncoderBar : public DisplayObject {
  private:
    bool lastButtonState = false;
    float *value;
    int lastValue;
    const uint16_t color;
    uint16_t fillColor;
    int minValue = 0;
    int maxValue = 100;
    bool mapToLeftLed = false;
    bool mapToRightLed = false;
    AiEsp32RotaryEncoder &encoder;

  public:
    struct Props {
        AiEsp32RotaryEncoder *encoder = nullptr;
        float *value;
        int16_t x = -1;
        int16_t y = -1;
        int16_t width = 10;
        int16_t height = 130;
        int minValue = 0;
        int maxValue = 100;
        bool mapToLeftLed = false;
        bool mapToRightLed = false;
    };

    explicit EncoderBar(const Props &props) : DisplayObject(props.x, props.y, props.width, props.height), value(props.value), color(ST77XX_WHITE), encoder(*props.encoder), mapToLeftLed(props.mapToLeftLed), mapToRightLed(props.mapToRightLed) {
        minValue = props.minValue;
        maxValue = props.maxValue;
    }

    void setValue(float *newValue) {
        value = newValue;
        isDirty = true;
    }

    void setColor(uint16_t newColor) {
        fillColor = newColor;
        isDirty = true;
    }

    bool shouldDraw() override {
        int currentValue = encoder.readEncoder();
        if (lastValue != currentValue) {
            return true;
        }
        return false;
    }

    void draw() override {
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            int rounding = min(height, width) / 2;
            lastValue = int(*value);
            float ratio = *value / float(maxValue);
            int drawHeight = (height - 2);
            int drawWidth = (width - 2);
            if (width > height) {
                drawWidth = drawWidth * ratio;
            } else {
                drawHeight = drawHeight * ratio;
            }
            tft.fillRect(x, y, width, height, ST77XX_BLACK);
            tft.fillRoundRect(x + 1, y + height - drawHeight - 1, drawWidth, drawHeight, rounding - 1, fillColor);
            tft.drawRoundRect(x, y, width, height, rounding, COLOR_WHITE);
        }
        updateLeds();
        xSemaphoreGive(displayMutex);
    }

  private:
    void updateLeds() {
        if (!mapToLeftLed && !mapToRightLed) return;
        if (mapToLeftLed) {
            setLeftEncoderLed(fillColor);
        }
        if (mapToRightLed) {
            setRightEncoderLed(fillColor);
        }
    }
};

#endif