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

extern void resetMiddleButtonCounter();

#define CHARACTERISTIC_ADVANCED_STATUS_UUID "4F53534D-6164-7661-6E63-656473746174"
#define CHARACTERISTIC_ADVANCED_CONFIG_UUID "4F53534D-6164-7661-6E63-6564636F6E66"
#define CHARACTERISTIC_ADVANCED_CONTROL_UUID "4F53534D-6164-7661-6E63-6564636F6D6D"

struct Control {
    float value;
    std::uint8_t minValue = 0;
    std::uint8_t maxValue = 100;
};

std::unordered_map<std::string, Control> advancedSettings;
std::vector<std::string> controlNames;
std::vector<std::string> modifierNames;

std::vector<uint16_t> advancedColors = {0xf860, 0xfc00, 0xffe0, 0x07e0, 0x07ff, 0x001f, 0xa87d};

class OSSMAdvanced : public Device {
  public:
    bool isFirstConnect = true;
    int baseIndex = 0;
    int modifierIndex = 0;
    GFXcanvas16 *canvas = new GFXcanvas16(300, 152);

    TextButton *pauseStopButton = nullptr;

    std::vector<TextButton *> buttons = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

    EncoderBar *speedBar = nullptr;
    EncoderBar *valueBar = nullptr;

    explicit OSSMAdvanced(const NimBLEAdvertisedDevice *advertisedDevice) : Device(advertisedDevice) {
        characteristics = {{"control", {NimBLEUUID(CHARACTERISTIC_ADVANCED_CONTROL_UUID)}},
                           {"config", {NimBLEUUID(CHARACTERISTIC_ADVANCED_CONFIG_UUID)}},
                           {"status", DeviceCharacteristics{NimBLEUUID(CHARACTERISTIC_ADVANCED_STATUS_UUID),
                                                            .notifyCallback = [this](NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) {
                                                                String test = String(reinterpret_cast<char *>(pData), length);
                                                                ESP_LOGD("CALLBACK", "Returned, value: %s", test.c_str());
                                                            }}}};
    }

    const char *getName() override { return "OSSM - Advanced Mode"; }
    NimBLEUUID getServiceUUID() override { return NimBLEUUID(OSSM_ADVANCED_SERVICE_ID); }

    float getMaxSteps() {
        uint8_t maxSteps = 4;
        for (u_int8_t c = 0; c < controlNames.size(); c++) {
            uint8_t modSteps = 0;
            for (u_int8_t m = 1; m < controlNames.size() - 1; m++) {
                modSteps += advancedSettings[controlNames[c] + modifierNames[m]].value;
            }
            maxSteps = max(maxSteps, modSteps);
        }
        return maxSteps;
    }

    void drawSingleModifier(uint8_t c, uint16_t x = 0, int16_t y = 0, int16_t width = 300, int16_t height = 150) {
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            float stepWidth = width / getMaxSteps();
            uint16_t lineColor = advancedColors[c];
            Control control = advancedSettings[controlNames[c]];

            float baseValueRatio = (1 - control.value / 100.0);
            float modValueRatio = (1 - advancedSettings[controlNames[c] + modifierNames[0]].value / 100.0);
            float strokeRatio = 1 - baseValueRatio;
            if (c < 2) {
                strokeRatio = (advancedSettings[controlNames[0]].value - advancedSettings[controlNames[1]].value) / 100.0;
            }
            uint16_t baseY = height * baseValueRatio + y;
            uint16_t modY = baseY + height * strokeRatio * modValueRatio;
            if (c == 1) {
                modY = baseY - height * strokeRatio * modValueRatio;
            }
            int startX = x - stepWidth * advancedSettings[controlNames[c] + modifierNames[5]].value;
            int m = 0;
            ESP_LOGI(TAG, "BASE: %d, MOD: %d", baseY, modY);
            while (startX < x + width) {
                uint16_t step = advancedSettings[controlNames[c] + modifierNames[m + 1]].value * stepWidth;
                switch (m) {
                    case 0:
                        canvas->drawLine(startX, baseY, startX + step, modY, lineColor);
                        break;
                    case 1:
                        canvas->drawLine(startX, modY, startX + step, modY, lineColor);
                        break;
                    case 2:
                        canvas->drawLine(startX, modY, startX + step, baseY, lineColor);
                        break;
                    case 3:
                        canvas->drawLine(startX, baseY, startX + step, baseY, lineColor);
                        break;
                }

                startX += step;
                m = (m + 1) % 4;
            }
        }
        xSemaphoreGive(displayMutex);
    }

    void drawModifierDisplay() {
        canvas->fillRect(0, 0, 300, 152, COLOR_BLACK);

        for (u_int8_t c = 0; c < controlNames.size(); c++) {
            if (c != baseIndex) {
                drawSingleModifier(c);
            }
        }
        drawSingleModifier(baseIndex);
        drawSingleModifier(baseIndex, 0, 1);
        drawSingleModifier(baseIndex, 1, 0);
        drawSingleModifier(baseIndex, 1, 1);
        tft.drawRGBBitmap(10, 60, canvas->getBuffer(), canvas->width(), canvas->height());
    }

    void drawCommonControls() {
        leftEncoder.setBoundaries(0, 100);
        leftEncoder.setAcceleration(50);
        rightEncoder.setBoundaries(0, 100);
        rightEncoder.setAcceleration(50);
        draw<TextButton>("<<", pins::BTN_L_SHOULDER, -5, -5, 70, 30);
        draw<TextButton>(">>", pins::BTN_R_SHOULDER, DISPLAY_WIDTH - 65, -5, 70, 30);
        speedBar = draw<EncoderBar>(EncoderBar::Props{.encoder = &leftEncoder, .value = &advancedSettings["SP"].value, .x = 0, .y = (int16_t)(Display::PageY + 35), .mapToLeftLed = true});
        speedBar->setColor(Colors::speed);
        valueBar = draw<EncoderBar>(EncoderBar::Props{
            .encoder = &rightEncoder, .value = &advancedSettings[controlNames[0]].value, .x = (int16_t)(DISPLAY_WIDTH - 10), .y = (int16_t)(Display::PageY + 35), .mapToRightLed = true});

        pauseStopButton = draw<TextButton>("Pause", pins::BTN_UNDER_C, DISPLAY_WIDTH / 2 - 60, Display::HEIGHT - 25, 120, 30);

        updateTabAppearance();
        syncRightEncoder();
        syncLeftEncoder();
    }

    void drawControls() override {
        const int16_t tabY = Display::StatusbarHeight;
        const int16_t tabHeight = 24;
        const int16_t tabGap = 0;
        const int16_t totalGaps = (controlNames.size() - 1) * tabGap;
        const int16_t tabWidth = (DISPLAY_WIDTH - totalGaps) / controlNames.size();
        for (int i = 0; i < controlNames.size(); i++) {
            buttons[i] = draw<TextButton>(String(int(advancedSettings[controlNames[i]].value)), NO_PIN, i * (tabWidth + tabGap), tabY, tabWidth, tabHeight);
        }

        draw<TextButton>("Presets", pins::BTN_UNDER_L, -5, Display::HEIGHT - 25, 90, 30);
        draw<TextButton>("Modifier", pins::BTN_UNDER_R, DISPLAY_WIDTH - 85, Display::HEIGHT - 25, 90, 30);

        drawCommonControls();
        onResume();
    }

    void onConnect() override {
        controlNames.clear();
        modifierNames.clear();
        advancedSettings.clear();
        std::string configValues = readString("config") + ',';
        std::string statusString = readString("status") + ',';
        std::string modifierString;
        int8_t ci = configValues.find(',', 0);
        int8_t si = statusString.find(',', 0);
        while (ci > 0) {
            std::string single = configValues.substr(0, ci);
            std::string singleStatus = statusString.substr(0, si);
            u8_t j = single.find('(');
            u8_t k = single.find('/');
            u8_t l = single.find(')');

            std::string name = single.substr(0, j);
            controlNames.push_back(name);

            float value = std::stof(singleStatus);
            ESP_LOGI(TAG, "String: %s, Value: %f", singleStatus.c_str(), value);
            u8_t minValue = std::stoi(single.substr(j + 1, k));
            u8_t maxValue = std::stoi(single.substr(k + 1, l));
            Control newControl = {value, minValue, maxValue};

            advancedSettings.emplace(name, newControl);

            int8_t mi = single.find(':');
            if (mi > 0) {
                modifierString = single.substr(l + 2) + ':';
            }
            mi = modifierString.find(':');
            std::string iterString = modifierString;
            while (mi > 0) {
                j = iterString.find('(');
                k = iterString.find('/');
                l = iterString.find(')');
                std::string modifierName = iterString.substr(0, j);
                if (std::find(modifierNames.begin(), modifierNames.end(), modifierName) == modifierNames.end()) {
                    modifierNames.push_back(modifierName);
                }
                newControl.minValue = std::stoi(iterString.substr(j + 1, k));
                newControl.maxValue = std::stoi(iterString.substr(k + 1, l));
                int8_t ms = singleStatus.find(':');
                if (ms > 0) {
                    singleStatus = singleStatus.substr(ms + 1);
                    newControl.value = std::stof(singleStatus);
                } else {
                    newControl.value = newControl.minValue;
                    if (modifierName == modifierNames[0]) {
                        newControl.value = newControl.maxValue;
                    }
                }
                advancedSettings.emplace(name + modifierName, newControl);

                iterString = iterString.substr(mi + 1);
                mi = iterString.find(':');
            }

            configValues = configValues.substr(ci + 1);
            statusString = statusString.substr(si + 1);
            ci = configValues.find(',', 0);
            si = statusString.find(',', 0);
        }
        controlNames.pop_back();

        menu.clear();
        settingsMenu.clear();
        this->menu.push_back(MenuItem{MenuItemE::DEVICE_MENU_ITEM, "placeholder", nullptr, "placeholder", .metaIndex = 0});
        this->settingsMenu.push_back(MenuItem{MenuItemE::DEVICE_MENU_ITEM, "placeholder", nullptr, "placeholder", .metaIndex = 0});

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
    }

    static void drawModifierTask(void *pvParameters) {
        bool lastLeftShoulderState = HIGH;
        bool lastRightShoulderState = HIGH;
        bool currentLeftShoulderState = HIGH;
        bool currentRightShoulderState = HIGH;
        int lastLeftEncoderValue = -1;
        int currentLeftEncoderValue = -1;
        int lastRightEncoderValue = -1;
        int currentRightEncoderValue = -1;

        while (stateMachine->is("device_menu"_s)) {
            if (device != nullptr) {
                currentLeftShoulderState = digitalRead(pins::BTN_L_SHOULDER);
                currentRightShoulderState = digitalRead(pins::BTN_R_SHOULDER);
                currentLeftEncoderValue = leftEncoder.readEncoder();
                currentRightEncoderValue = rightEncoder.readEncoder();

                if (currentLeftShoulderState == LOW && lastLeftShoulderState == HIGH) {
                    device->onLeftBumperClick();
                }
                if (currentRightShoulderState == LOW && lastRightShoulderState == HIGH) {
                    device->onRightBumperClick();
                }
                if (currentLeftEncoderValue != lastLeftEncoderValue) {
                    setNotIdle("left_encoder");
                    device->onLeftEncoderChange(currentLeftEncoderValue);
                    lastLeftEncoderValue = currentLeftEncoderValue;
                }
                if (currentRightEncoderValue != lastRightEncoderValue) {
                    setNotIdle("right_encoder");
                    device->onRightEncoderChange(currentRightEncoderValue);
                    lastRightEncoderValue = currentRightEncoderValue;
                }
                for (auto &displayObject : device->displayObjects) {
                    displayObject->tick();
                }
                lastLeftShoulderState = currentLeftShoulderState;
                lastRightShoulderState = currentRightShoulderState;
            }
            vTaskDelay(16 / portTICK_PERIOD_MS);
        }

        vTaskDelete(NULL);
    }

    void drawDeviceSettingsMenu() override {
        clearPage();

        draw<TextButton>("Back", pins::BTN_UNDER_L, -5, Display::HEIGHT - 25, 90, 30);
        draw<TextButton>("Select", pins::BTN_UNDER_R, DISPLAY_WIDTH - 85, Display::HEIGHT - 25, 90, 30);

        drawCommonControls();
    }

    void drawDeviceMenu() override {
        clearPage();

        const int16_t tabY = Display::StatusbarHeight;
        const int16_t tabHeight = 24;
        const int16_t tabGap = 0;
        const int16_t totalGaps = (modifierNames.size() - 1) * tabGap;
        const int16_t tabWidth = (DISPLAY_WIDTH - totalGaps) / modifierNames.size();
        for (int i = 0; i < modifierNames.size(); i++) {
            buttons[i] = draw<TextButton>(String(int(advancedSettings[controlNames[baseIndex] + modifierNames[i]].value)), NO_PIN, i * (tabWidth + tabGap), tabY, tabWidth, tabHeight);
        }

        draw<TextButton>("Back", pins::BTN_UNDER_L, -5, Display::HEIGHT - 25, 90, 30);
        draw<TextButton>("Back", pins::BTN_UNDER_R, DISPLAY_WIDTH - 85, Display::HEIGHT - 25, 90, 30);

        drawCommonControls();

        drawModifierDisplay();

        vTaskDelay(10 / portTICK_PERIOD_MS);
        xTaskCreatePinnedToCore(drawModifierTask, "drawModifierTask", 5 * configMINIMAL_STACK_SIZE, device, 5, NULL, 1);
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

    bool setModifierValue(int value) {
        std::string c = controlNames[baseIndex] + modifierNames[modifierIndex];
        Control *edit = &advancedSettings[c];
        value = constrain(value, edit->minValue, edit->maxValue);
        if (value == edit->value && !hasRightEncoderChanged(true)) {
            return 1;
        }
        edit->value = value;
        drawModifierDisplay();
        return send("control", std::to_string(baseIndex) + ":" + std::to_string(modifierIndex) + ":" + std::to_string(value) + ",");
    }

    bool setBaseValue(int value) {
        Control *edit = &advancedSettings[controlNames[baseIndex]];
        value = constrain(value, edit->minValue, edit->maxValue);
        if (value == edit->value && !hasRightEncoderChanged(true)) {
            return 1;
        }
        edit->value = value;
        if (baseIndex == 0) {
            advancedSettings[controlNames[1]].maxValue = value;
        }
        if (baseIndex == 1) {
            advancedSettings[controlNames[0]].minValue = value;
        }
        return send("control", std::to_string(baseIndex) + ":" + std::to_string(value) + ",");
    }

    void syncRightEncoder() {
        std::string s = controlNames[baseIndex];
        if (stateMachine->is("device_menu"_s)) {
            s += modifierNames[modifierIndex];
        }
        Control *c = &advancedSettings[s];
        rightEncoder.setBoundaries(c->minValue, c->maxValue);
        rightEncoder.setEncoderValue(c->value);
        if (s == controlNames[1]) {
            valueBar->setMinMax(c->minValue, 100);
        } else {
            valueBar->setMinMax(c->minValue, c->maxValue);
        }
        valueBar->setValue(&c->value);
    };

    void syncLeftEncoder() { leftEncoder.setEncoderValue(advancedSettings["SP"].value); }

    void onLeftBumperClick() override {
        if (stateMachine->is("device_menu"_s)) {
            modifierIndex = (modifierIndex + modifierNames.size() - 1) % modifierNames.size();
        } else {
            baseIndex = (baseIndex + controlNames.size() - 1) % controlNames.size();
            modifierIndex = 0;
        }
        syncRightEncoder();
        updateTabAppearance();
    }

    void onRightBumperClick() override {
        if (stateMachine->is("device_menu"_s)) {
            modifierIndex = (modifierIndex + 1) % modifierNames.size();
            drawModifierDisplay();
        } else {
            baseIndex = (baseIndex + 1) % controlNames.size();
            modifierIndex = 0;
        }
        syncRightEncoder();
        updateTabAppearance();
    }

    void onRightEncoderChange(int value) override {
        if (stateMachine->is("device_menu"_s)) {
            buttons[modifierIndex]->setText(String(value));
            setModifierValue(value);
            speedBar->isFirstDraw = true;
            return;
        }
        buttons[baseIndex]->setText(String(value));
        setBaseValue(value);
    }

    void onLeftEncoderChange(int value) override {
        setSpeed(value);
        if (isPaused && value > 0) {
            onResume();
        }
    }

  private:
    void updateTabAppearance() {
        for (int i = 0; i < controlNames.size(); i++) {
            buttons[i]->setColors(Colors::disabled, Colors::black);
        }
        int index = baseIndex;
        uint16_t newColor = advancedColors[index];
        if (stateMachine->is("device_menu"_s)) {
            index = modifierIndex;
        }
        buttons[index]->setColors(newColor, Colors::black);

        valueBar->setColor(newColor);
    }
};

#endif
