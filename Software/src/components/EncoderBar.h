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
#include <memory>
#include <vector>

class EncoderBar : public DisplayObject {
  private:
    float *value;
    int lastValue;
    const uint16_t color;
    uint16_t fillColor;
    int minValue = 0;
    int maxValue = 100;
    bool mapToLeftLed = false;
    bool mapToRightLed = false;
    AiEsp32RotaryEncoder &encoder;
    std::unique_ptr<GFXcanvas16> canvas;

  public:
    struct Props {
        AiEsp32RotaryEncoder *encoder = nullptr;
        float *value;
        int16_t pos_x = -1;
        int16_t pos_y = -1;
        int16_t w = 10;
        int16_t h = 140;
        int minValue = 0;
        int maxValue = 100;
        bool mapToLeftLed = false;
        bool mapToRightLed = false;
    };

    explicit EncoderBar(const Props &props)
        : DisplayObject(props.pos_x, props.pos_y, props.w, props.h),
          value(props.value),
          color(ST77XX_WHITE),
          minValue(props.minValue),
          maxValue(props.maxValue),
          mapToLeftLed(props.mapToLeftLed),
          mapToRightLed(props.mapToRightLed),
          encoder(*props.encoder),
          canvas(std::make_unique<GFXcanvas16>(props.w, props.h)) {}

    void setValue(float *newValue) {
        value = newValue;
        isDirty = true;
    }

    void setMinMax(int newMinValue, int newMaxValue) {
        minValue = newMinValue;
        maxValue = newMaxValue;
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
            canvas->fillRect(0, 0, width, height, ST77XX_BLACK);
            canvas->fillRoundRect(1, height - drawHeight - 1, drawWidth, drawHeight, rounding - 1, fillColor);
            canvas->drawRoundRect(0, 0, width, height, rounding, COLOR_WHITE);
            tft.drawRGBBitmap(x, y, canvas->getBuffer(), canvas->width(), canvas->height());
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