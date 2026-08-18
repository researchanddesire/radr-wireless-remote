#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_ST7789.h>
#include "pins.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Global TFT instance
extern Adafruit_ST7789 tft;

// Display semaphore for thread-safe access
extern SemaphoreHandle_t displayMutex;

// Brightness constants
constexpr uint8_t BRIGHTNESS_FULL = 255;
constexpr uint8_t BRIGHTNESS_DIM = 25;
constexpr uint8_t BRIGHTNESS_OFF = 0;

// Initialize the display
bool initDisplay();

// Set screen brightness (0-255)
void setScreenBrightness(uint8_t brightness);
uint8_t getScreenBrightness();

// Convenience functions
void dimScreen();
void restoreScreenBrightness();
void turnOffScreen();
void drawImage(const uint16_t* imageData, uint16_t width, uint16_t height);

#endif
