#include "leds.h"
#include "utils/psramTask.h"
#include <Arduino.h>

#include <constants/Colors.h>
#include <optional>

#include "esp_log.h"

static const char* ledsTaskName = "ledsTask";

static auto TAG = "LED";

// Individual LED control state
struct IndividualLedState {
    bool isIndividuallyControlled = false;
    CRGB color = CRGB::Black;
};

IndividualLedState individualLedStates[pins::NUM_LEDS];
CRGB leds[pins::NUM_LEDS];
float ledPaceSpeedRpm = 60.0;  // Default value, adjust as needed
struct LedState {
    std::optional<double> targetColor = std::nullopt;
    std::optional<double> currentColor = std::nullopt;
    double targetBrightness = 0;
    double currentBrightness = 0;

    double progress = 0.0;
    double startColor = 0;
    double startBrightness = 0;
    long startTime = 0;
    uint16_t duration_ms = 250;
};

LedState ledState;

// ease in out sine
double ease(const double t) { return 0.5 * (1 + sin(3.1415926 * (t - 0.5))); }

void ledsTask(void* pvParameters) {
    auto* state = static_cast<LedState*>(pvParameters);
    bool shouldDraw = true;

    while (true) {
        shouldDraw = state->progress < 1.0 &&
                     (state->currentColor != state->targetColor ||
                      state->currentBrightness != state->targetBrightness);

        if (!shouldDraw) {
            vTaskDelay(250 / portTICK_PERIOD_MS);
            continue;
        }

        if (state->duration_ms == 0) {
            state->progress = 1.0;
        } else {
            state->progress =
                constrain(static_cast<double>(millis() - state->startTime) /
                              static_cast<double>(state->duration_ms),
                          0.0, 1.0);
        }

        const float dir =
            state->startColor - state->targetColor.value_or(0) <= 128 ? 1 : -1;

        const double easeProgress = ease(state->progress);

        state->currentColor =
            (state->startColor) +
            dir * easeProgress *
                (state->targetColor.value_or(0) - state->startColor);

        state->currentBrightness =
            state->startBrightness +
            (state->targetBrightness - state->startBrightness) * easeProgress;

        if (state->progress >= 1.0) {
            state->currentColor = state->targetColor;
            state->currentBrightness = state->targetBrightness;
            state->progress = 1.0;
            state->startColor = state->targetColor.value_or(0);
        }

        leds[0] = individualLedStates[0].isIndividuallyControlled
                      ? individualLedStates[0].color
                      : CHSV(state->currentColor.value_or(0), 255,
                             state->currentBrightness);
        leds[1] = individualLedStates[1].isIndividuallyControlled
                      ? individualLedStates[1].color
                      : CHSV(state->currentColor.value_or(0), 255,
                             state->currentBrightness);
        leds[2] = individualLedStates[2].isIndividuallyControlled
                      ? individualLedStates[2].color
                      : CHSV(state->currentColor.value_or(0), 255,
                             state->currentBrightness);
        FastLED.show();

        vTaskDelay(1);
    }
}

void initFastLEDs() {
    ESP_LOGI(TAG, "Initializing FastLEDs...");
    CFastLED::addLeds<LED_TYPE, pins::LED_PIN, COLOR_ORDER>(leds,
                                                            pins::NUM_LEDS)
        .setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.show();

#ifdef DEBUG_LEDS
    ESP_LOGI(TAG, "DEBUG_LEDS is defined, setting leds to red and green");
    // set the leds to red
    setLed(RED, 255);
    FastLED.show();
    vTaskDelay(1000);
    setLed(GREEN, 255);
    FastLED.show();
    vTaskDelay(1000);
    // then clear
    // setLedOff(leds);
#endif

    ESP_LOGI(TAG, "FastLEDs initialization complete.");

    createPsramTask(ledsTask, ledsTaskName, 5 * configMINIMAL_STACK_SIZE,
                    &ledState, tskIDLE_PRIORITY, nullptr, 1);

    vTaskDelay(100);
    setLed(LEDColors::logoBlue, 255, 1500);
}

void setLed(uint8_t color_value, const uint8_t brightness,
            const uint16_t duration_ms) {
    if (ledState.targetColor.value_or(0) == color_value &&
        ledState.targetBrightness == brightness) {
        return;
    }

    ledState.targetColor = static_cast<double>(color_value);
    ledState.targetBrightness = static_cast<double>(brightness);
    ledState.duration_ms = duration_ms;
    ledState.progress = 0.0;
    ledState.startColor = ledState.currentColor.value_or(color_value);
    ledState.startBrightness = ledState.currentBrightness;
    ledState.startTime = millis();
}

void setLedOff() {
    ledState.targetColor = std::nullopt;
    ledState.targetBrightness = 0;
    ledState.progress = 0.0;
    ledState.startTime = millis();
    ledState.duration_ms = 0;
}

// Helper function to convert RGB565 to RGB888 with maximum color preservation
void rgb565ToRgb(uint16_t rgb565, uint8_t& r, uint8_t& g, uint8_t& b) {
    // Extract components
    uint8_t r5 = (rgb565 >> 11) & 0x1F;
    uint8_t g6 = (rgb565 >> 5) & 0x3F;
    uint8_t b5 = rgb565 & 0x1F;

    // Use bit shifting with full range mapping to prevent desaturation
    r = (r5 << 3) | (r5 >> 2);  // 5-bit to 8-bit: ensures 31->255
    g = (g6 << 2) | (g6 >> 4);  // 6-bit to 8-bit: ensures 63->255
    b = (b5 << 3) | (b5 >> 2);  // 5-bit to 8-bit: ensures 31->255

    // Apply gamma correction to boost saturation
    r = (r * r) >> 8;  // Square for more vivid colors
    g = (g * g) >> 8;
    b = (b * b) >> 8;
}

// Set individual LED color using RGB565 format
void setIndividualLed(uint8_t ledIndex, uint16_t rgb565Color,
                      uint8_t brightness) {
    // TODO: Future work if LED config is added - need to either allow override
    // or always respect config

    if (ledIndex >= pins::NUM_LEDS) return;

    uint8_t r, g, b;
    rgb565ToRgb(rgb565Color, r, g, b);
    individualLedStates[ledIndex].color = CRGB(r, g, b);
    individualLedStates[ledIndex].color.fadeLightBy(255 - brightness);
    individualLedStates[ledIndex].isIndividuallyControlled = true;

    // Update the LED immediately
    leds[ledIndex] = individualLedStates[ledIndex].color;
    FastLED.show();
}

// Set left encoder LED (LED 0)
void setLeftEncoderLed(uint16_t rgb565Color, uint8_t brightness) {
    setIndividualLed(0, rgb565Color, brightness);
}

// Set middle LED (LED 1)
void setMiddleLed(uint16_t rgb565Color, uint8_t brightness) {
    setIndividualLed(1, rgb565Color, brightness);
}

// Set right encoder LED (LED 2)
void setRightEncoderLed(uint16_t rgb565Color, uint8_t brightness) {
    setIndividualLed(2, rgb565Color, brightness);
}

// Release individual LED control back to global LED task
void releaseIndividualLed(uint8_t ledIndex) {
    if (ledIndex >= pins::NUM_LEDS) return;
    individualLedStates[ledIndex].isIndividuallyControlled = false;
    individualLedStates[ledIndex].color = CRGB::Black;
}

// Release all individual LEDs back to global control
void releaseAllIndividualLeds() {
    for (uint8_t i = 0; i < pins::NUM_LEDS; i++) {
        releaseIndividualLed(i);
    }
}
