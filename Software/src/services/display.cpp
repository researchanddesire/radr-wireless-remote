#include "display.h"
#include "esp_log.h"
#include <vector>

static const char* TAG = "DISPLAY";

// PWM configuration for backlight
// LEDC channel allocation (see also any global PWM/channel docs):
//   - Channel 0 : reserved by Tone library
//   - Channel 7 : reserved for TFT backlight (BACKLIGHT_PWM_CHANNEL)
//
// NOTE: Do not reuse channel 7 elsewhere; allocate new LEDC channels only
// after checking and updating this documented mapping.
static const int BACKLIGHT_PWM_CHANNEL = 7;
static const int BACKLIGHT_PWM_FREQ =  5000; // 5kHz - high enough to avoid flicker
static const int BACKLIGHT_PWM_RESOLUTION = 8; // 8-bit resolution (0-255)

// Track current brightness for restore functionality
static uint8_t currentBrightness = BRIGHTNESS_FULL;

// Screen setup
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, pins::TFT_CS, pins::TFT_DC, pins::TFT_RST);

// Create the display mutex
SemaphoreHandle_t displayMutex = xSemaphoreCreateMutex();

bool initDisplay()
{
    // Initialize PWM for backlight control
    ledcSetup(BACKLIGHT_PWM_CHANNEL, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_RESOLUTION);
    ledcAttachPin(pins::TFT_BL, BACKLIGHT_PWM_CHANNEL);
    ledcWrite(BACKLIGHT_PWM_CHANNEL, BRIGHTNESS_FULL);
    currentBrightness = BRIGHTNESS_FULL;

    SPI.begin(pins::TFT_SCLK, -1, pins::TFT_MOSI, pins::TFT_CS);
    // Increase SPI frequency for faster display updates (40MHz max for ST7789)
    SPI.setFrequency(40000000); // 40MHz
    tft.init(240, 320); // Initialize with screen dimensions
    tft.setRotation(1); // Landscape mode
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);

    return true;
}

void setScreenBrightness(uint8_t brightness)
{
    ledcWrite(BACKLIGHT_PWM_CHANNEL, brightness);
    currentBrightness = brightness;
}

uint8_t getScreenBrightness() { return currentBrightness; }

void dimScreen()
{
    setScreenBrightness(BRIGHTNESS_DIM);
}

void restoreScreenBrightness()
{
    setScreenBrightness(BRIGHTNESS_FULL);
}

void turnOffScreen()
{
    setScreenBrightness(BRIGHTNESS_OFF);
}

void drawImage(const uint16_t* imageData, uint16_t width, uint16_t height) {
    const uint8_t LINES_PER_CHUNK = 4;
    const uint16_t BUFFER_SIZE = width * LINES_PER_CHUNK;
    std::vector<uint16_t> buffer(BUFFER_SIZE);
    for (uint16_t row = 0; row < height; row += LINES_PER_CHUNK) {
        uint16_t lines = (row + LINES_PER_CHUNK > height) ? (height - row) : LINES_PER_CHUNK;
        const int xOffset = (320 - (int)width) / 2;
        const int yOffset = (240 - (int)height) / 2;
        memcpy_P(buffer.data(), &imageData[row * width], BUFFER_SIZE * sizeof(uint16_t));
        tft.drawRGBBitmap(xOffset, row + yOffset, buffer.data(), width, lines);
    }
}
