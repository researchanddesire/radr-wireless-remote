#include <Arduino.h>
#include <LittleFS.h>
#include <OneButton.h>

#include "DeviceEmulator.h"
#include "EmulatorDisplay.h"

#ifndef EMULATOR_START_INDEX
#define EMULATOR_START_INDEX 0
#endif

static const int BUTTON_PIN = 35;

static DeviceEmulator emulator;
static OneButton button(BUTTON_PIN, true, true);

static void refreshDisplay() {
    drawEmulatorScreen(emulator);
}

static void handleButtonClick() {
    ESP_LOGI("MAIN", "Button pressed - cycling to next device");
    emulator.nextDevice();
}

void setup() {
    Serial.begin(115200);
    delay(500);
    ESP_LOGI("MAIN", "ButtplugIO Device Emulator starting...");

    initEmulatorDisplay();
    drawLoadingScreen("Initializing...");

    if (!LittleFS.begin(true)) {
        ESP_LOGE("MAIN", "LittleFS mount failed");
        drawLoadingScreen("LittleFS FAILED!");
        while (true) vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    drawLoadingScreen("Loading devices...");

    if (!emulator.init()) {
        ESP_LOGE("MAIN", "No devices loaded from registry");
        drawLoadingScreen("No devices found!");
        while (true) vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    ESP_LOGI("MAIN", "Loaded %d devices", emulator.getDeviceCount());
    emulator.setDisplayCallback(refreshDisplay);

    button.attachClick(handleButtonClick);

    int startIdx = EMULATOR_START_INDEX;
    if (startIdx >= emulator.getDeviceCount()) startIdx = 0;

    drawLoadingScreen("Starting BLE...");
    emulator.startDevice(startIdx);
}

void loop() {
    button.tick();
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
