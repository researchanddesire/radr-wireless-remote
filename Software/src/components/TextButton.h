#ifndef TextButton_h
#define TextButton_h

#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>  // Match genericPages button font

#include "../services/display.h"
#include "DisplayObject.h"
#include "constants/Colors.h"

extern Adafruit_ST7789 tft;
extern SemaphoreHandle_t displayMutex;

// Special constant to indicate no pin assignment (visual-only button)
#define NO_PIN 255

class TextButton : public DisplayObject {
  private:
    bool lastButtonState = false;
    String buttonText;
    const uint8_t buttonPin;

    // Dynamic styling properties
    uint16_t textColor = Colors::black;
    uint16_t backgroundColor = Colors::textBackground;
    uint16_t pressedTextColor = Colors::black;
    uint16_t pressedBackgroundColor = Colors::white;

    // Track changes for redraw
    String lastText;
    uint16_t lastTextColor;
    uint16_t lastBackgroundColor;

    GFXcanvas16 *canvas;

  public:
    TextButton(const String &text, uint8_t pin, int16_t x, int16_t y, int16_t width = 70, int16_t height = 35) : DisplayObject(x, y, width, height), buttonText(text), buttonPin(pin) {
        // Initialize tracking variables
        lastText = text;
        lastTextColor = textColor;
        lastBackgroundColor = backgroundColor;
        canvas = new GFXcanvas16(width, height);
    }

    ~TextButton() { delete canvas; }

    // Methods to dynamically change button appearance
    void setText(const String &text) { buttonText = text; }

    void setColors(uint16_t backgroundColor = Colors::textBackground, uint16_t textColor = Colors::white) {
        this->textColor = textColor;
        this->backgroundColor = backgroundColor;
    }

    bool shouldDraw() override {
        bool currentState = false;
        bool stateChanged = false;

        // Only check pin state if a valid pin is assigned
        if (buttonPin != NO_PIN) {
            currentState = digitalRead(buttonPin) == LOW;
            stateChanged = currentState != lastButtonState;
        }

        bool textChanged = buttonText != lastText;
        bool colorsChanged = (textColor != lastTextColor || backgroundColor != lastBackgroundColor);

        return stateChanged || textChanged || colorsChanged || isFirstDraw;
    }

    void draw() override {
        bool currentState = false;

        // Only check pin state if a valid pin is assigned
        if (buttonPin != NO_PIN) {
            currentState = digitalRead(buttonPin) == LOW;
        }

        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            // Clear the button area
            canvas->fillRect(0, 0, width, height, ST77XX_BLACK);

            canvas->setFont(&FreeSans9pt7b);

            if (!currentState) {
                // Use filled rectangle for improved visuals
                //(border-only near physical screen border can cause visual
                // artifacts)
                canvas->fillRoundRect(0, 0, width, height, 5, backgroundColor);
                canvas->setTextColor(textColor);
            } else {
                canvas->fillRoundRect(0, 0, width, height, 5, pressedBackgroundColor);
                canvas->setTextColor(pressedTextColor);
            }

            // Calculate text position for proper centering (matching
            // genericPages.cpp style)
            int16_t x1, y1;
            uint16_t textWidth, textHeight;
            canvas->getTextBounds(buttonText.c_str(), 0, 0, &x1, &y1, &textWidth, &textHeight);

            // Horizontal centering
            int16_t textX = (width - textWidth) / 2;

            // Vertical centering - using same formula as Device Stopped screen
            // buttons
            int16_t textY = (height + textHeight) / 2;

            canvas->setCursor(textX, textY);
            canvas->print(buttonText);
            tft.drawRGBBitmap(x, y, canvas->getBuffer(), canvas->width(), canvas->height());

            xSemaphoreGive(displayMutex);
        }

        // Update tracking variables
        lastButtonState = currentState;
        lastText = buttonText;
        lastTextColor = textColor;
        lastBackgroundColor = backgroundColor;
        isFirstDraw = false;
    }
};

#endif