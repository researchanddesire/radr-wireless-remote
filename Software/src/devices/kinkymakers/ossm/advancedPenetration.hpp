#pragma once

#ifndef OSSM_ADVANCED_DEVICE_H
#define OSSM_ADVANCED_DEVICE_H

#include <ArduinoJson.h>
#include <components/DynamicText.h>
#include <components/EncoderBar.h>
#include <components/LinearRailGraph.h>
#include <components/TextButton.h>
#include <memory>
#include <pages/menus.h>
#include <services/leds.h>

#include "../../device.h"
#include "state/remote.h"

extern void resetMiddleButtonCounter();

#define CHARACTERISTIC_ADVANCED_STATUS_UUID "4F53534D-6164-7661-6E63-656473746174"
#define CHARACTERISTIC_ADVANCED_CONFIG_UUID "4F53534D-6164-7661-6E63-6564636F6E66"
#define CHARACTERISTIC_ADVANCED_CONTROL_UUID "4F53534D-6164-7661-6E63-6564636F6E74"
#define CHARACTERISTIC_ADVANCED_PRESETS_UUID "4F53534D-6164-7661-6E63-656470727374"

class OSSMAdvanced : public Device {
  public:
    bool isFirstConnect = true;
    int8_t readCount = 0;
    uint8_t baseIndex = 0;
    uint8_t modifierIndex = 0;
    const int16_t tabY = Display::StatusbarHeight;
    const int16_t tabHeight = 24;
    const int16_t tabGap = 4;

    struct Control {
        float value;
        std::uint8_t minValue = 0;
        std::uint8_t maxValue = 100;
    };

    std::unordered_map<std::string, Control> advancedSettings;
    std::vector<std::string> controlNames;
    std::vector<std::string> modifierNames;

    std::vector<uint16_t> advancedColors = {0xf860, 0xfc00, 0xffe0, 0x07e0, 0x001f, 0xa87d, 0xf81f};

    std::unique_ptr<GFXcanvas16> canvas = std::make_unique<GFXcanvas16>(300, 152);

    TextButton *pauseStopButton = nullptr;

    std::vector<TextButton *> buttons = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

    EncoderBar *speedBar = nullptr;
    EncoderBar *valueBar = nullptr;

    explicit OSSMAdvanced(const NimBLEAdvertisedDevice *advertisedDevice) : Device(advertisedDevice) {
        characteristics = {
            {"control", {NimBLEUUID(CHARACTERISTIC_ADVANCED_CONTROL_UUID)}},
            {"config", {NimBLEUUID(CHARACTERISTIC_ADVANCED_CONFIG_UUID)}},
            {"presets", {NimBLEUUID(CHARACTERISTIC_ADVANCED_PRESETS_UUID)}},
            {"status", DeviceCharacteristics{NimBLEUUID(CHARACTERISTIC_ADVANCED_STATUS_UUID),
                                             .notifyCallback = [this](NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
                                                                      size_t length, bool isNotify) { readCount++; }}}};
    }

    const char *getName() override { return "OSSM - Advanced Mode"; }
    NimBLEUUID getServiceUUID() override { return NimBLEUUID(OSSM_ADVANCED_SERVICE_ID); }

    float getMaxSteps() {
        uint8_t maxSteps = 4;
        for (std::string controlName : controlNames) {
            uint8_t modSteps = 0;
            for (u_int8_t m = 1; m < controlNames.size() - 2; m++) {
                modSteps += advancedSettings[controlName + modifierNames[m]].value;
            }
            maxSteps = max(maxSteps, modSteps);
        }
        return maxSteps;
    }

    void drawSingleModifier(uint8_t c, uint16_t x = 0, int16_t y = 0, int16_t width = 300, int16_t height = 150) {
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

    void drawCanvasToDisplay() {
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            tft.drawRGBBitmap(10, 60, canvas->getBuffer(), canvas->width(), canvas->height());
        }
        xSemaphoreGive(displayMutex);
    }

    void drawModifierDisplay() {
        canvas->fillRect(0, 0, 300, 152, COLOR_BLACK);

        for (u_int8_t c = 0; c < controlNames.size() - 1; c++) {
            drawSingleModifier(c);
        }
        drawSingleModifier(baseIndex, 0, 1);
        drawSingleModifier(baseIndex, 1, 0);
        drawSingleModifier(baseIndex, 1, 1);
        drawCanvasToDisplay();
    }

    float bezierMath(float v0, float v1, float v2, float v3, float t) {
        float output = pow(1 - t, 3) * v0;
        output += 3 * pow(1 - t, 2) * t * v1;
        output += 3 * (1 - t) * pow(t, 2) * v2;
        output += pow(t, 3) * v3;
        return output;
    }

    void bezCurve(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, float r, float t, uint16_t color = COLOR_WHITE) {
        float diff = (x1 - x0) * r;
        uint16_t x = floor(bezierMath(x0, x0 + diff, x1 - diff, x1, t));
        uint16_t y = floor(bezierMath(y0, y0, y1, y1, t));
        canvas->drawPixel(x, y, color);
    }

    void drawCurveDisplay() {
        canvas->fillRect(0, 0, 300, 152, COLOR_BLACK);
        float y1 = 150 - advancedSettings[controlNames[0]].value * 1.5;
        float y0 = 150 - advancedSettings[controlNames[1]].value * 1.5;

        float diff = y0 - y1;
        float y1m = (1 - advancedSettings[controlNames[0] + modifierNames[0]].value / 100.0) * diff + y1;
        float y0m = y0 - (1 - advancedSettings[controlNames[1] + modifierNames[0]].value / 100.0) * diff;

        float a = advancedSettings[controlNames[2]].value;
        float am = advancedSettings[controlNames[2] + modifierNames[0]].value / 100.0 * a;
        float b = advancedSettings[controlNames[3]].value;
        float bm = advancedSettings[controlNames[3] + modifierNames[0]].value / 100.0 * b;
        uint16_t x = (1 - (a / b) / (a / b + 1)) * 300;
        uint16_t xm = (1 - (am / bm) / (am / bm + 1)) * 300;

        float c = advancedSettings[controlNames[4]].value;
        float cm = advancedSettings[controlNames[4] + modifierNames[0]].value / 100.0 * c;
        float d = advancedSettings[controlNames[5]].value;
        float dm = advancedSettings[controlNames[4] + modifierNames[0]].value / 100.0 * d;

        uint16_t inColor = COLOR_WHITE;
        uint16_t outColor = COLOR_WHITE;

        switch (baseIndex) {
            case 0:
                canvas->drawFastHLine(0, y1, 300, advancedColors[baseIndex]);
                if (y1 != y1m) {
                    for (uint16_t p = 0; p <= 300; p += 5) {
                        canvas->drawPixel(p, y1m, advancedColors[baseIndex]);
                    }
                }
                break;
            case 1:
                canvas->drawFastHLine(0, y0, 300, advancedColors[baseIndex]);
                if (y0 != y0m) {
                    for (uint16_t p = 0; p <= 300; p += 5) {
                        canvas->drawPixel(p, y0m, advancedColors[baseIndex]);
                    }
                }
                break;
            case 2:
            case 3:
                canvas->drawFastVLine(x, 0, 150, advancedColors[baseIndex]);
                if (x != xm) {
                    for (uint16_t p = 0; p <= 150; p += 5) {
                        canvas->drawPixel(xm, p, advancedColors[baseIndex]);
                    }
                }
                break;
            case 4:
                inColor = advancedColors[baseIndex];
                break;
            case 5:
                outColor = advancedColors[baseIndex];
                break;
        }
        for (uint16_t p = 0; p <= x; p += 1) {
            bezCurve(0, x, y0, y1, 0.1 + 0.4 * (1 - c / 100.0), p / float(x), inColor);
        }
        for (uint16_t p = 0; p <= (300 - x); p += 1) {
            bezCurve(300, x, y0, y1, 0.1 + 0.4 * (1 - d / 100.0), p / float(300 - x), outColor);
        }
        if (y0 != y0m || y1 != y1m) {
            for (uint16_t p = 0; p <= xm; p += 5) {
                bezCurve(0, xm, y0m, y1m, 0.1 + 0.4 * (1 - cm / 100.0), p / float(xm), inColor);
            }
            for (uint16_t p = 0; p <= (300 - xm); p += 5) {
                bezCurve(300, xm, y0m, y1m, 0.1 + 0.4 * (1 - dm / 100.0), p / float(300 - xm), outColor);
            }
        }

        drawCanvasToDisplay();
    }

    void drawCommonControls() {
        leftEncoder.setBoundaries(0, 100);
        leftEncoder.setAcceleration(50);
        rightEncoder.setBoundaries(0, 100);
        rightEncoder.setAcceleration(50);
        draw<TextButton>("<<", pins::BTN_L_SHOULDER, -5, -5, 70, 30);
        draw<TextButton>(">>", pins::BTN_R_SHOULDER, DISPLAY_WIDTH - 65, -5, 70, 30);
        speedBar = draw<EncoderBar>(EncoderBar::Props{.encoder = &leftEncoder,
                                                      .value = &advancedSettings["SP"].value,
                                                      .pos_x = 0,
                                                      .pos_y = (int16_t)(Display::PageY + 35),
                                                      .mapToLeftLed = true});
        speedBar->setColor(advancedColors[6]);
        valueBar = draw<EncoderBar>(EncoderBar::Props{.encoder = &rightEncoder,
                                                      .value = &advancedSettings[controlNames[0]].value,
                                                      .pos_x = (int16_t)(DISPLAY_WIDTH - 10),
                                                      .pos_y = (int16_t)(Display::PageY + 35),
                                                      .mapToRightLed = true});

        pauseStopButton = draw<TextButton>("Pause", pins::BTN_UNDER_C, DISPLAY_WIDTH / 2 - 60, Display::HEIGHT - 25, 120, 30);

        updateTabAppearance();
        syncRightEncoder();
        syncLeftEncoder();
    }

    void drawControls() override {
        device->displayObjects.clear();
        uint8_t totalGaps = (controlNames.size() - 2) * tabGap;
        uint8_t tabWidth = (DISPLAY_WIDTH - totalGaps) / (controlNames.size() - 1);
        for (uint8_t i = 0; i < controlNames.size() - 1; i++) {
            buttons[i] = draw<TextButton>(String(int(advancedSettings[controlNames[i]].value)), NO_PIN, i * (tabWidth + tabGap), tabY,
                                          tabWidth, tabHeight);
        }

        draw<TextButton>("Presets", pins::BTN_UNDER_L, -5, Display::HEIGHT - 25, 90, 30);
        draw<TextButton>("Modifier", pins::BTN_UNDER_R, DISPLAY_WIDTH - 85, Display::HEIGHT - 25, 90, 30);
        drawCurveDisplay();

        drawCommonControls();
        onResume();
    }

    void parseStatus() {
        int8_t controlCounter = 0;
        std::string statusString = readString("status") + ',';
        int8_t si = statusString.find(',', 0);
        while (si > 0) {
            std::string singleStatus = statusString.substr(0, si);
            float value = std::stof(singleStatus);
            advancedSettings[controlNames[controlCounter]].value = value;

            int8_t mi = singleStatus.find(':');
            int8_t modifierCounter = 0;
            if (mi == -1) {
                advancedSettings[controlNames[controlCounter] + modifierNames[0]].value = 100;
            }
            while (mi > 0) {
                singleStatus = singleStatus.substr(mi + 1);
                value = std::stof(singleStatus);
                advancedSettings[controlNames[controlCounter] + modifierNames[modifierCounter]].value = value;
                modifierCounter++;
                mi = singleStatus.find(':');
            }
            statusString = statusString.substr(si + 1);
            si = statusString.find(',', 0);
            controlCounter++;
        }
        readCount = 0;
    }

    void parseConfig() {
        controlNames.clear();
        modifierNames.clear();
        advancedSettings.clear();
        std::string configValues = readString("config") + ',';
        std::string modifierString;
        int8_t ci = configValues.find(',', 0);
        while (ci > 0) {
            std::string singleConfig = configValues.substr(0, ci);
            u8_t j = singleConfig.find('(');
            u8_t k = singleConfig.find('/');
            u8_t l = singleConfig.find(')');
            std::string name = singleConfig.substr(0, j);
            controlNames.emplace_back(name);
            u8_t minValue = std::stoi(singleConfig.substr(j + 1, k));
            u8_t maxValue = std::stoi(singleConfig.substr(k + 1, l));
            Control newControl = {float(minValue), minValue, maxValue};
            advancedSettings.try_emplace(name, newControl);
            int8_t mi = singleConfig.find(':');
            if (mi > 0) {
                modifierString = singleConfig.substr(l + 2) + ':';
            }
            mi = modifierString.find(':');
            std::string iterString = modifierString;
            while (mi > 0) {
                j = iterString.find('(');
                k = iterString.find('/');
                l = iterString.find(')');
                std::string modifierName = iterString.substr(0, j);
                if (std::find(modifierNames.begin(), modifierNames.end(), modifierName) == modifierNames.end()) {
                    modifierNames.emplace_back(modifierName);
                }
                newControl.minValue = std::stoi(iterString.substr(j + 1, k));
                newControl.maxValue = std::stoi(iterString.substr(k + 1, l));
                newControl.value = newControl.minValue;
                if (modifierName == modifierNames[0]) {
                    newControl.value = newControl.maxValue;
                }
                advancedSettings.try_emplace(name + modifierName, newControl);
                iterString = iterString.substr(mi + 1);
                mi = iterString.find(':');
            }
            configValues = configValues.substr(ci + 1);
            ci = configValues.find(',', 0);
        }
    }

    void setButtonsText() {
        if (stateMachine->is("device_menu"_s)) {
            for (uint8_t i = 0; i < modifierNames.size(); i++) {
                buttons[i]->setText(String(int(advancedSettings[controlNames[baseIndex] + modifierNames[i]].value)));
            }
            drawModifierDisplay();
        } else {
            for (int i = 0; i < controlNames.size() - 1; i++) {
                buttons[i]->setText(String(int(advancedSettings[controlNames[i]].value)));
            }
            drawCurveDisplay();
        }
    }

    void dirtyRunner() {
        while (true) {
            if (readCount > 0) {
                parseStatus();
                syncRightEncoder();
                syncLeftEncoder();
                setButtonsText();
            }
            vTaskDelay(100);
        }
        vTaskDelete(NULL);
    }

    void loadPresets() {
        settingsMenu.clear();
        std::string presetList = readString("presets");
        int8_t pi = presetList.find(',', 0);
        while (pi > 0) {
            std::string presetName = presetList.substr(0, pi);
            this->settingsMenu.emplace_back(MenuItem{MenuItemE::DEVICE_MENU_ITEM, presetName, researchAndDesireWaves});

            presetList = presetList.substr(pi + 1);
            pi = presetList.find(',', 0);
        }
        this->settingsMenu.emplace_back(MenuItem{MenuItemE::DEVICE_MENU_ITEM, "Save New Preset", researchAndDesireWaves});
    }

    static void runDirtyRunnerTask(void *pvParameters) { static_cast<OSSMAdvanced *>(pvParameters)->dirtyRunner(); }

    void onConnect() override {
        parseConfig();
        parseStatus();

        loadPresets();
        menu.clear();
        this->menu.emplace_back(MenuItem{MenuItemE::DEVICE_MENU_ITEM, "placeholder", nullptr, "placeholder"});

        isConnected = true;
        isFirstConnect = false;

        xTaskCreatePinnedToCore(runDirtyRunnerTask, "dirtyRunner", 5 * configMINIMAL_STACK_SIZE, device, 5, NULL, 1);
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

    void onDeviceMenuItemSelected(int index) override {
        if (activeMenu != nullptr) {
            if (index < settingsMenu.size() - 1) {
                std::string name = settingsMenu[index].name;
                send("presets", ":" + name);
            } else {
                send("presets", ">");
                loadPresets();
            }
            parseStatus();
        }
    }

    void drawDeviceSettingsMenu() override {
        device->displayObjects.clear();
        activeMenu = &settingsMenu;
        activeMenuCount = settingsMenu.size();
        drawMenu();
        TextButton back("Back", pins::BTN_UNDER_L, -5, Display::HEIGHT - 25, 90, 30);
        TextButton select("Select", pins::BTN_UNDER_R, DISPLAY_WIDTH - 85, Display::HEIGHT - 25, 90, 30);
        pauseStopButton = draw<TextButton>("Pause", pins::BTN_UNDER_C, DISPLAY_WIDTH / 2 - 60, Display::HEIGHT - 25, 120, 30);
        back.tick();
        select.tick();
        pauseStopButton->tick();
    }

    void drawDeviceMenu() override {
        activeMenu = nullptr;
        clearPage();
        device->displayObjects.clear();

        uint8_t totalGaps = (modifierNames.size() - 1) * tabGap;
        uint8_t tabWidth = (DISPLAY_WIDTH - totalGaps) / modifierNames.size();
        for (uint8_t i = 0; i < modifierNames.size(); i++) {
            buttons[i] = draw<TextButton>(String(int(advancedSettings[controlNames[baseIndex] + modifierNames[i]].value)), NO_PIN,
                                          i * (tabWidth + tabGap), tabY, tabWidth, tabHeight);
        }

        draw<TextButton>("Back", pins::BTN_UNDER_L, -5, Display::HEIGHT - 25, 90, 30);
        draw<TextButton>("Back", pins::BTN_UNDER_R, DISPLAY_WIDTH - 85, Display::HEIGHT - 25, 90, 30);

        drawCommonControls();

        drawModifierDisplay();

        vTaskDelay(10 / portTICK_PERIOD_MS);
        xTaskCreatePinnedToCore(drawModifierTask, "drawModifierTask", 5 * configMINIMAL_STACK_SIZE, device, 5, NULL, 1);
    }

    bool setSpeed(uint8_t speed) {
        Control *edit = &advancedSettings["SP"];
        speed = constrain(speed, edit->minValue, edit->maxValue);
        if (speed == edit->value && !hasLeftEncoderChanged(true)) {
            return true;
        }
        edit->value = speed;
        readCount = -2;
        return send("control", std::string("6:") + std::to_string(speed) + ",");
    }

    bool setModifierValue(uint8_t value) {
        std::string c = controlNames[baseIndex] + modifierNames[modifierIndex];
        Control *edit = &advancedSettings[c];
        value = constrain(value, edit->minValue, edit->maxValue);
        if (value == edit->value && !hasRightEncoderChanged(true)) {
            return true;
        }
        edit->value = value;
        drawModifierDisplay();
        readCount = -2;
        return send("control", std::to_string(baseIndex) + ":" + std::to_string(modifierIndex) + ":" + std::to_string(value) + ",");
    }

    bool setBaseValue(uint8_t value) {
        Control *edit = &advancedSettings[controlNames[baseIndex]];
        value = constrain(value, edit->minValue, edit->maxValue);
        if (value == edit->value && !hasRightEncoderChanged(true)) {
            return true;
        }
        edit->value = value;
        if (baseIndex == 0) {
            advancedSettings[controlNames[1]].maxValue = value;
        }
        if (baseIndex == 1) {
            advancedSettings[controlNames[0]].minValue = value;
        }
        drawCurveDisplay();
        readCount = -2;
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
            drawModifierDisplay();
        } else {
            baseIndex = (baseIndex + controlNames.size() - 2) % (controlNames.size() - 1);
            modifierIndex = 0;
            drawCurveDisplay();
        }
        syncRightEncoder();
        updateTabAppearance();
    }

    void onRightBumperClick() override {
        if (stateMachine->is("device_menu"_s)) {
            modifierIndex = (modifierIndex + 1) % modifierNames.size();
            drawModifierDisplay();
        } else {
            baseIndex = (baseIndex + 1) % (controlNames.size() - 1);
            modifierIndex = 0;
            drawCurveDisplay();
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
        for (int i = 0; i < controlNames.size() - 1; i++) {
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
