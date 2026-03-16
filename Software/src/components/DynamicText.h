#ifndef DYNAMICTEXT_H
#define DYNAMICTEXT_H

#include "DisplayObject.h"
#include "constants/Colors.h"
#include "services/display.h"
#include <Strings.h>

class DynamicText : public DisplayObject {
  private:
    static constexpr const char *TAG = "DynamicText";
    const std::string &text;
    std::string lastValue = ui::strings::EMPTY_STRING;
    uint16_t currentTextColor;
    uint16_t lastTextColor;

    // get text bounds
    int16_t x1, y1;
    uint16_t textWidth, textHeight;

  public:
    DynamicText(const std::string &text, int16_t x, int16_t y, uint16_t color = Colors::textBackground)
        : DisplayObject(x, y, text.length() * 8, 16), text(text), currentTextColor(color), lastTextColor(color) {
        lastValue = text;
    }

    // Method to set the text color dynamically
    void setColor(uint16_t color) {
        currentTextColor = color;
    }

    bool shouldDraw() override { 
        return isFirstDraw || text != lastValue || currentTextColor != lastTextColor; 
    }

    void draw() override {
        ESP_LOGI(TAG, "Drawing DynamicText: %s", text.c_str());
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            tft.setFont(&FreeSans9pt7b);
            tft.setTextColor(currentTextColor);

            // Get bounds for both old and new text to calculate actual drawing positions
            int16_t oldX1, oldY1, newX1, newY1;
            uint16_t oldWidth, oldHeight, newWidth, newHeight;

            // Calculate actual drawing positions for both texts
            int oldDrawX, newDrawX;
            if (x == -1) {
                // Centered positioning
                tft.getTextBounds(lastValue.c_str(), 0, y, &oldX1, &oldY1, &oldWidth, &oldHeight);
                tft.getTextBounds(text.c_str(), 0, y, &newX1, &newY1, &newWidth, &newHeight);
                oldDrawX = (Display::WIDTH - oldWidth) / 2;
                newDrawX = (Display::WIDTH - newWidth) / 2;
            } else {
                // Fixed positioning
                tft.getTextBounds(lastValue.c_str(), x, y, &oldX1, &oldY1, &oldWidth, &oldHeight);
                tft.getTextBounds(text.c_str(), x, y, &newX1, &newY1, &newWidth, &newHeight);
                oldDrawX = x;
                newDrawX = x;
            }

            // Calculate combined clearing area using actual drawing positions
            int16_t clearX = min(oldDrawX + oldX1, newDrawX + newX1);
            int16_t clearY = min(oldY1, newY1);
            uint16_t clearWidth = max(oldDrawX + oldX1 + oldWidth, newDrawX + newX1 + newWidth) - clearX;
            uint16_t clearHeight = max(oldY1 + oldHeight, newY1 + newHeight) - clearY;

            // Clear the combined area
            tft.fillRect(clearX, clearY, clearWidth, clearHeight, Colors::black);

            // Draw the new text at the calculated position
            tft.setCursor(newDrawX, y);
            tft.print(text.c_str());

            xSemaphoreGive(displayMutex);
        }

        lastValue = text;
        lastTextColor = currentTextColor;
    };
};

#endif  // DYNAMICTEXT_H