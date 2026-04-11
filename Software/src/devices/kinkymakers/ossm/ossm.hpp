#pragma once

#ifndef OSSM_ADVANCED_DEVICE_H
#define OSSM_ADVANCED_DEVICE_H

#include <ArduinoJson.h>
#include <components/DynamicText.h>
#include <components/EncoderBar.h>
#include <components/LinearRailGraph.h>
#include <components/TextButton.h>
#include <pages/menus.h>
#include <services/leds.h>

#include "../../device.h"
#include "state/remote.h"
// Forward declaration for button counter reset function
extern void resetMiddleButtonCounter();

#define CHARACTERISTIC_ADVANCED_STATUS_UUID "4F53534D-6164-7661-6E63-656473746174"
#define CHARACTERISTIC_ADVANCED_CONFIG_UUID "4F53534D-6164-7661-6E63-6564636F6E66"
#define CHARACTERISTIC_ADVANCED_CONTROL_UUID "4F53534D-6164-7661-6E63-6564636F6D6D"

std::vector<std::string> controlNames;

struct Control {
    float value;
    std::uint8_t minValue = 0;
    std::uint8_t maxValue = 100;
};

std::vector<uint16_t> buttonColors = {0xf860, 0xfc00, 0xffe0, 0x07e0, 0x07ff, 0x001f, 0xa87d};
std::unordered_map<std::string, Control> advancedSettings;

class OSSMAdvanced : public Device {
  public:
    bool isFirstConnect = true;
    int focusedIndex = 0;

    TextButton *menuButton = nullptr;
    TextButton *pauseStopButton = nullptr;

    std::vector<TextButton *> buttons = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

    EncoderBar *speedBar = nullptr;
    EncoderBar *valueBar = nullptr;

    explicit OSSMAdvanced(const NimBLEAdvertisedDevice *advertisedDevice) : Device(advertisedDevice) {
        characteristics = {
            {"control", {NimBLEUUID(CHARACTERISTIC_ADVANCED_CONTROL_UUID)}},
            {"config", {NimBLEUUID(CHARACTERISTIC_ADVANCED_CONFIG_UUID)}},
            {"status", {NimBLEUUID(CHARACTERISTIC_ADVANCED_STATUS_UUID)}},
        };
        ESP_LOGI(TAG, "explicit ");
    }

    const char *getName() override { return "OSSM - Advanced Mode"; }
    NimBLEUUID getServiceUUID() override { return NimBLEUUID(OSSM_ADVANCED_SERVICE_ID); }

    void drawControls() override {
        leftEncoder.setBoundaries(0, 100);
        leftEncoder.setAcceleration(50);

        rightEncoder.setBoundaries(0, 100);
        rightEncoder.setAcceleration(50);

        const int16_t tabY = Display::StatusbarHeight;
        const int16_t tabHeight = 24;
        const int16_t tabGap = 0;
        const int16_t totalGaps = (controlNames.size() - 1) * tabGap;
        const int16_t tabWidth = (DISPLAY_WIDTH - totalGaps) / controlNames.size();

        for (int i = 0; i < controlNames.size(); i++) {
            buttons[i] = draw<TextButton>(String(controlNames[i].c_str()), NO_PIN, i * (tabWidth + tabGap), tabY, tabWidth, tabHeight);
        }

        draw<TextButton>("<<", pins::BTN_L_SHOULDER, -5, -5);
        draw<TextButton>(">>", pins::BTN_R_SHOULDER, DISPLAY_WIDTH - 65, -5);

        menuButton = draw<TextButton>("Menu", pins::BTN_UNDER_L, -5, Display::HEIGHT - 30, 90);
        pauseStopButton = draw<TextButton>("Pause", pins::BTN_UNDER_C, DISPLAY_WIDTH / 2 - 60, Display::HEIGHT - 30, 120);
        draw<TextButton>("Presets", pins::BTN_UNDER_R, DISPLAY_WIDTH - 85, Display::HEIGHT - 30, 90);

        if (menuButton) {
            menuButton->setColors(Colors::disabled, Colors::black);
        }

        speedBar = draw<EncoderBar>(EncoderBar::Props{.encoder = &leftEncoder, .value = &advancedSettings["SP"].value, .x = 5, .y = (int16_t)(Display::PageY + 35), .width = 20, .mapToLeftLed = true});
        speedBar->setColor(Colors::speed);
        valueBar = draw<EncoderBar>(EncoderBar::Props{.encoder = &rightEncoder, .value = &advancedSettings[controlNames[0]].value, .x = (int16_t)(DISPLAY_WIDTH - 10 - 5), .y = (int16_t)(Display::PageY + 35), .mapToRightLed = true});

        updateTabAppearance();
        syncRightEncoder();
        syncLeftEncoder();

        onResume();
    }

    void onConnect() override {
        // when we connect, pull the current state from the device
        std::string configValues = readString("config") + ',';
        std::string statusString = readString("status") + ',';
        int8_t ci = configValues.find(',', 0);
        int8_t si = statusString.find(',', 0);
        while (ci > 0) {
            std::string single = configValues.substr(0, ci);
            u8_t j = single.find('(');
            u8_t k = single.find('/');
            u8_t l = single.find(')');
            std::string name = single.substr(0, j);
            float value = std::stof(statusString.substr(0, si));
            u8_t minValue = std::stoi(single.substr(j + 1, k));
            u8_t maxValue = std::stoi(single.substr(k + 1, l));
            Control newControl = {value, minValue, maxValue};
            controlNames.push_back(name);
            advancedSettings.emplace(name, newControl);
            configValues = configValues.substr(ci + 1);
            statusString = statusString.substr(si + 1);
            ci = configValues.find(',', 0);
            si = statusString.find(',', 0);
        }
        controlNames.pop_back();

        isConnected = true;
        isFirstConnect = false;
    }

    void onPause(bool fullStop = false) override {
        playBuzzerPattern(BuzzerPattern::PAUSED);
        isPaused = true;
        setSpeed(0);
        leftEncoder.setEncoderValue(0);

        if (displayObjects.empty()) {
            if (fullStop) {
                rightEncoder.setEncoderValue(0);
            }
            return;
        }

        if (pauseStopButton) {
            pauseStopButton->setText("STOP");
            pauseStopButton->setColors(Colors::red, Colors::white);
        }

        // TODO: Uncomment this when functionality is added for OSSM Device Menu
        // Enable menu button when paused
        // if (menuButton) {
        //     menuButton->setColors(Colors::textBackground, Colors::black);
        // }

        setMiddleLed(Colors::red, 255);

        if (fullStop) {
            rightEncoder.setEncoderValue(0);
        }
    }

    void onResume() override {
        isPaused = false;
        resetMiddleButtonCounter();
        setMiddleLed(Colors::white, 50);

        if (displayObjects.empty()) {
            return;
        }

        if (pauseStopButton) {
            pauseStopButton->setText("Pause");
            pauseStopButton->setColors(Colors::textBackground, Colors::black);
        }

        if (menuButton) {
            menuButton->setColors(Colors::disabled, Colors::black);
        }
    }

    void drawDeviceMenu() override {
        activeMenu = &menu;
        activeMenuCount = menu.size();

        // // Find the menu index that corresponds to the current pattern
        // currentOption = 0;  // Default to first item
        // for (int i = 0; i < menu.size(); i++) {
        //     if (menu[i].metaIndex == static_cast<int>(settings.pattern)) {
        //         currentOption = i;
        //         break;
        //     }
        // }

        drawMenu();
    }

    void dumpValues(std::string when) {
        for (int i = 0; i < controlNames.size(); i++) {
            std::string s = controlNames[i];
            Control *edit = &advancedSettings[s];
            ESP_LOGI(TAG, "%s, Dump: %s, Value: %f", when.c_str(), s.c_str(), edit->value);
        }
    }

    bool setSpeed(int speed) {
        Control *edit = &advancedSettings["SP"];
        speed = constrain(speed, edit->minValue, edit->maxValue);
        if (speed == edit->value && !hasLeftEncoderChanged(true)) {
            return true;
        }
        edit->value = speed;
        return send("control", std::string("6:") + std::to_string(speed) + ",");
    }

    bool setBaseValue(int value) {
        Control *edit = &advancedSettings[controlNames[focusedIndex]];
        value = constrain(value, edit->minValue, edit->maxValue);
        if (value == edit->value && !hasRightEncoderChanged(true)) {
            return 1;
        }
        edit->value = value;
        return send("control", std::to_string(focusedIndex) + ":" + std::to_string(value) + ",");
    }

    void syncRightEncoder() {
        std::string s = controlNames[focusedIndex];
        Control *c = &advancedSettings[s];
        valueBar->setValue(&c->value);
        rightEncoder.setEncoderValue(c->value);
        rightEncoder.setBoundaries(c->minValue, c->maxValue);
    };

    void syncLeftEncoder() { leftEncoder.setEncoderValue(advancedSettings["SP"].value); }

    void onLeftBumperClick() override {
        focusedIndex = (focusedIndex + controlNames.size() - 1) % controlNames.size();
        syncRightEncoder();
        updateTabAppearance();
    }

    void onRightBumperClick() override {
        focusedIndex = (focusedIndex + 1) % controlNames.size();
        syncRightEncoder();
        updateTabAppearance();
    }

    void onRightEncoderChange(int value) override { setBaseValue(value); }

    void onLeftEncoderChange(int value) override {
        setSpeed(value);
        if (isPaused && value > 0) {
            onResume();
        }
    }

    bool needsPersistentLeftEncoderMonitoring() const override { return true; }

  private:
    void updateTabAppearance() {
        for (int i = 0; i < controlNames.size(); i++) {
            buttons[i]->setColors(Colors::disabled, Colors::black);
        }
        uint16_t newColor = buttonColors[focusedIndex];
        buttons[focusedIndex]->setColors(newColor, Colors::white);

        valueBar->setColor(newColor);
    }
};

#endif
