#pragma once

#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <constants/Sizes.h>
#include <devices/device.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <pages/displayUtils.h>
#include <pages/genericPages.h>
#include <pins.h>
#include <qrcode.h>
#include <services/buzzer.h>
#include <services/display.h>
#include <services/encoder.h>
#include <services/leds.h>
#include <services/sleepWakeup.h>
#include <services/wm.h>

#include "components/TextButton.h"
#include "events.hpp"
#include "pages/TextPages.h"
#include "pages/controller.h"
#include "pages/menus.h"
#include "services/leftEncoderMonitor.h"

// Forward declarations to avoid circular dependencies
struct DiscoveredDevice;
std::vector<DiscoveredDevice> &getDiscoveredDevices();
void clearDiscoveredDevices();
void connectToDiscoveredDevice(int index);
void startScanWithTimeout(int timeoutMs, void (*onComplete)());
void onScanComplete();

namespace actions {

    auto clearPage = [](bool clearStatusbar = false) {
        // small delay to ensure tasks are finished
        vTaskDelay(50 / portTICK_PERIOD_MS);
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (clearStatusbar) {
                tft.fillRect(0, 0, Display::WIDTH, Display::HEIGHT,
                             Colors::black);
            } else {
                tft.fillRect(0, Display::StatusbarHeight, Display::WIDTH,
                             Display::PageHeight + 32, Colors::black);
                // Also clear top left and top right corners to remove buttons
                tft.fillRect(0, 0, 75, Display::StatusbarHeight, Colors::black);
                tft.fillRect(Display::WIDTH - 75, 0, 75,
                             Display::StatusbarHeight, Colors::black);
            }
            xSemaphoreGive(displayMutex);
        }
    };

    auto clearScreen = []() {
        // small delay to ensure tasks are finished
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            tft.fillScreen(Colors::black);
            xSemaphoreGive(displayMutex);
        }
    };

    auto disconnect = []() {
        // Safety-critical: Ensure left encoder monitoring is stopped if the
        // device needs it
        if (device != nullptr &&
            device->needsPersistentLeftEncoderMonitoring()) {
            stopLeftEncoderMonitoring();
        }

        if (device != nullptr) {
            delete device;  // Properly calls destructor AND frees memory
            device = nullptr;
        }

        // and then stop scanning.
        NimBLEScan *pScan = NimBLEDevice::getScan();
        pScan->stop();
        playBuzzerPattern(BuzzerPattern::DEVICE_DISCONNECTED);
        setLed(LEDColors::logoBlue, 255, 1500);
    };

    auto drawPage = [](const TextPage &page) {
        // Capture reference to static const object - safe since it lives in
        // flash memory
        return [&page]() {
            clearPage();
            xTaskCreatePinnedToCore(drawPageTask, "drawPageTask",
                                    5 * configMINIMAL_STACK_SIZE,
                                    const_cast<TextPage *>(&page), 5, NULL, 1);
        };
    };

    auto drawControl = []() {
        // Safety-critical: Ensure left encoder monitoring is active if the
        // device needs it
        if (device != nullptr &&
            device->needsPersistentLeftEncoderMonitoring()) {
            startLeftEncoderMonitoring();
        }

        // Single task creation with immediate UI rendering
        xTaskCreatePinnedToCore(drawControllerTask, "drawControllerTask",
                                16 * configMINIMAL_STACK_SIZE, device, 5, NULL,
                                1);
    };

    auto search = []() { startScanWithTimeout(5000, onScanComplete); };

    auto drawDeviceList = []() {
        // Stop scanning when we enter the device list
        NimBLEScan *pScan = NimBLEDevice::getScan();
        if (pScan->isScanning()) {
            pScan->stop();
        }

        // This will be implemented in menus.cpp
        drawDeviceListMenu();
    };

    auto play = [](BuzzerPattern pattern) {
        return [](BuzzerPattern pattern) { playBuzzerPattern(pattern); };
    };

    constexpr auto startTask = [](auto task, const char *taskName,
                                  TaskHandle_t handle, uint8_t size = 10,
                                  uint8_t core = 1) {
        return [task, taskName, handle, size, core]() mutable {
            xTaskCreatePinnedToCore(task, taskName,
                                    size * configMINIMAL_STACK_SIZE, nullptr, 1,
                                    &handle, core);
        };
    };

    auto selectDevice = []() { connectToDiscoveredDevice(currentOption); };

    auto clearDeviceList = []() { clearDiscoveredDevices(); };

    auto stop = []() {
        if (device == nullptr) {
            return;
        }
        device->onPause(true);
    };

    auto softPause = []() {
        if (device == nullptr) {
            return;
        }
        device->onPause();
    };

    auto start = []() {
        if (device == nullptr) {
            return;
        }
        device->onConnect();
    };

    auto drawDeviceMenu = []() { device->drawDeviceMenu(); };

    auto drawDeviceSettingsMenu = []() { device->drawDeviceSettingsMenu(); };

    auto onDeviceMenuItemSelected = []() {
        device->onDeviceMenuItemSelected(currentOption);
    };

    auto checkForUpdate = []() {
        // TODO: basically just say yes.
    };

    auto drawMainMenu = []() {
        // Release all individual LED controls back to global control
        releaseAllIndividualLeds();
        setLed(LEDColors::idle, 50,
               1500);  // Soft white idle (Blends with backlight bleed)
        activeMenu = &mainMenu;
        activeMenuCount = numMainMenu;
        clearPage();
        drawMenu();
    };

    auto drawSettingsMenu = []() {
        activeMenu = &settingsMenu;
        activeMenuCount = numSettingsMenu;
        clearPage();
        drawMenu();
    };

    auto espRestart = []() {
        playBuzzerPattern(BuzzerPattern::SHUTDOWN);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    };

    auto espSilentRestart = []() { esp_restart(); };

    auto startWiFiPortal = []() {
        // Give a second for any pending MQTT messages to be sent before
        // disconnecting WiFi Otherwise we lose this state_change message until
        // the device comes back online.
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        // disconnect from the network
        WiFi.disconnect(true);

        wm.setConfigPortalBlocking(false);
        wm.setConnectTimeout(30);
        wm.setConnectRetries(5);
        wm.setEnableConfigPortal(true);
        wm.setCleanConnect(true);
        wm.startConfigPortal("RADR Setup");

        // if the wifi is not currently connected then make a small task the
        // looks for the wifi connection and sends an event.
    };

    auto stopWiFiPortal = []() {
        wm.setConfigPortalBlocking(true);
        wm.stopConfigPortal();
    };

    auto enterDeepSleep = []() {
        // Disconnect from any connected devices first
        disconnect();

        // Turn off display backlight and other peripherals
        playBuzzerPattern(BuzzerPattern::SHUTDOWN);
        setScreenBrightness(BRIGHTNESS_OFF);
        clearScreen();
        setLedOff();

        // Give time for buzzer to finish
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        // Disable encoder interrupts before sleep to prevent conflicts
        detachInterrupt(digitalPinToInterrupt(pins::LEFT_ENCODER_A));
        detachInterrupt(digitalPinToInterrupt(pins::LEFT_ENCODER_B));
        detachInterrupt(digitalPinToInterrupt(pins::RIGHT_ENCODER_A));
        detachInterrupt(digitalPinToInterrupt(pins::RIGHT_ENCODER_B));

        // Configure GPIO wake-up sources for light sleep
        gpio_wakeup_enable(static_cast<gpio_num_t>(pins::BTN_UNDER_C),
                           GPIO_INTR_LOW_LEVEL);
        gpio_wakeup_enable(static_cast<gpio_num_t>(pins::BTN_UNDER_L),
                           GPIO_INTR_LOW_LEVEL);
        gpio_wakeup_enable(static_cast<gpio_num_t>(pins::BTN_UNDER_R),
                           GPIO_INTR_LOW_LEVEL);

        // Enable GPIO wake-up
        esp_sleep_enable_gpio_wakeup();

        // Use light sleep - more reliable wake-up
        esp_light_sleep_start();

        // Skip GPIO wake-up cleanup - just restart immediately to avoid
        // conflicts The restart will clean up everything properly
        espSilentRestart();
    };

}  // namespace actions