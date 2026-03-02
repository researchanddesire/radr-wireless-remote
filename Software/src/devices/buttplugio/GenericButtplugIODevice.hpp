#ifndef GENERIC_BUTTPLUGIO_DEVICE_HPP
#define GENERIC_BUTTPLUGIO_DEVICE_HPP

#include <Arduino.h>
#include <constants.h>
#include <constants/Sizes.h>

#include <components/BarChart.h>
#include <components/TextButton.h>
#include <devices/device.h>

#include "buttplugIOProtocol.hpp"
#include "services/display.h"
#include "services/encoder.h"

static const char *GENERIC_TAG = "GENERIC_BPIO";

static constexpr int MAX_DISPLAY_FEATURES = 6;

class GenericButtplugIODevice : public Device, public ButtplugIoProtocol {
  public:
    GenericButtplugIODevice(const NimBLEAdvertisedDevice *advertisedDevice,
                            const String &configFileName,
                            const JsonObjectConst &characteristicsConfig)
        : Device(advertisedDevice),
          ButtplugIoProtocol(configFileName, characteristicsConfig) {
        ESP_LOGI(GENERIC_TAG, "GenericButtplugIODevice constructor");

        String characteristicsConfigString = "";
        serializeJson(config, characteristicsConfigString);
        ESP_LOGI(GENERIC_TAG, "Characteristics config: %s",
                 characteristicsConfigString.c_str());

        String tx = config["tx"].as<String>();
        String rx = config["rx"].as<String>();

        characteristics = {{"tx", {NimBLEUUID(tx.c_str())}}};

        if (rx.length() > 0 && rx != "null") {
            characteristics["rx"] = DeviceCharacteristics{
                NimBLEUUID(rx.c_str()),
                .notifyCallback =
                    [this](NimBLERemoteCharacteristic *pRemoteCharacteristic,
                           uint8_t *pData, size_t length, bool isNotify) {
                        rxValue =
                            String(reinterpret_cast<char *>(pData), length);
                        ESP_LOGD(GENERIC_TAG, "RX: %s", rxValue.c_str());
                    }};
        }

        loadDisplayName();
    }

    String rxValue = "";
    int focusedIndex = 0;
    int leftFocusedIndex = 0;
    std::vector<float> featureValues = {};
    std::vector<BarChart *> barCharts = {};

    String getIdentifier() override {
        rxValue = "";
        const TickType_t checkInterval = 250 / portTICK_PERIOD_MS;
        TickType_t lastSendTick = xTaskGetTickCount();
        TickType_t startTick = xTaskGetTickCount();
        const TickType_t timeout = 5000 / portTICK_PERIOD_MS;

        do {
            vTaskDelay(50 / portTICK_PERIOD_MS);
            TickType_t currentTick = xTaskGetTickCount();
            if ((currentTick - lastSendTick) >= checkInterval) {
                send("tx", "DeviceType;");
                lastSendTick = currentTick;
            }
            if ((currentTick - startTick) >= timeout) {
                ESP_LOGW(GENERIC_TAG, "Identifier timeout, using defaults");
                return "";
            }
        } while (rxValue.isEmpty());

        auto deviceType = rxValue;
        int colonIndex = deviceType.indexOf(':');
        if (colonIndex != -1) {
            return deviceType.substring(0, colonIndex);
        }
        return deviceType.substring(0, 1);
    }

    void onConnect() override {
        if (protocol.isNull()) {
            String identifier = getIdentifier();
            if (identifier.isEmpty()) {
                setProtocolFromDefaults();
            } else {
                setProtocol(identifier);
            }
        }

        initFeatureValues();
        sendAllFeatures(true);
        isConnected = true;
    }

    NimBLEUUID getServiceUUID() override {
        return advertisedDevice->getServiceUUID();
    }

    const char *getName() override {
        return getDeviceName().length() > 0 ? getDeviceName().c_str()
                                            : "Device";
    }

    void drawControls() override {
        const auto &feats = getFeatures();
        int numFeatures = min((int)feats.size(), MAX_DISPLAY_FEATURES);

        if (numFeatures == 0) {
            ESP_LOGW(GENERIC_TAG, "No output features to display");
            return;
        }

        int activeFeat = min(focusedIndex, numFeatures - 1);
        leftEncoder.setBoundaries(feats[activeFeat].minValue,
                                  feats[activeFeat].maxValue);
        leftEncoder.setAcceleration(feats[activeFeat].maxValue > 50 ? 100 : 0);
        if (activeFeat < (int)featureValues.size()) {
            leftEncoder.setEncoderValue((int)featureValues[activeFeat]);
        }

        // Shoulder buttons for cycling features (only if >1 feature)
        if (numFeatures > 1) {
            draw<TextButton>("<<", pins::BTN_L_SHOULDER, -5, -5);
            draw<TextButton>(">>", pins::BTN_R_SHOULDER, DISPLAY_WIDTH - 65, -5);
        }

        // Bar charts stacked vertically in the page area
        int barHeight = 22;
        int barGap = 4;
        int totalBarsHeight = numFeatures * barHeight + (numFeatures - 1) * barGap;
        int startY = Display::PageY +
                     (Display::PageHeight - 35 - totalBarsHeight) / 2;
        startY = max((int)Display::PageY + 4, startY);
        int barWidth = DISPLAY_WIDTH - 10;
        int barX = 5;

        barCharts.clear();
        for (int i = 0; i < numFeatures; i++) {
            int yPos = startY + i * (barHeight + barGap);
            auto *bar = draw<BarChart>(
                feats[i].name.c_str(),
                &featureValues[i],
                feats[i].minValue,
                feats[i].maxValue,
                barX, yPos, barWidth, barHeight,
                i == focusedIndex);
            barCharts.push_back(bar);
        }

        draw<TextButton>("STOP", pins::BTN_UNDER_C, DISPLAY_WIDTH / 2 - 60,
                         DISPLAY_HEIGHT - 30, 120);
    }

    void onLeftEncoderChange(int value) override {
        const auto &feats = getFeatures();
        if (focusedIndex >= (int)featureValues.size()) return;
        if (focusedIndex >= (int)feats.size()) return;

        featureValues[focusedIndex] = constrain(
            value, feats[focusedIndex].minValue, feats[focusedIndex].maxValue);

        sendFeature(focusedIndex);
    }

    void onLeftBumperClick() override {
        const auto &feats = getFeatures();
        int n = min((int)feats.size(), MAX_DISPLAY_FEATURES);
        if (n <= 1) return;

        focusedIndex = (focusedIndex + n - 1) % n;
        syncEncoder();
        updateBarFocus();
    }

    void onRightBumperClick() override {
        const auto &feats = getFeatures();
        int n = min((int)feats.size(), MAX_DISPLAY_FEATURES);
        if (n <= 1) return;

        focusedIndex = (focusedIndex + 1) % n;
        syncEncoder();
        updateBarFocus();
    }

    void onPause(bool fullStop) override {
        sendAllFeatures(true);
        vTaskDelay(250 / portTICK_PERIOD_MS);
        sendAllFeatures(true);
    }

  protected:
    void initFeatureValues() {
        const auto &feats = getFeatures();
        featureValues.resize(feats.size(), 0);
        for (size_t i = 0; i < feats.size(); i++) {
            featureValues[i] = feats[i].minValue;
        }
    }

    void syncEncoder() {
        const auto &feats = getFeatures();
        if (focusedIndex >= (int)feats.size()) return;

        leftEncoder.setBoundaries(feats[focusedIndex].minValue,
                                  feats[focusedIndex].maxValue);
        leftEncoder.setAcceleration(
            feats[focusedIndex].maxValue > 50 ? 100 : 0);

        if (focusedIndex < (int)featureValues.size()) {
            leftEncoder.setEncoderValue((int)featureValues[focusedIndex]);
        }
    }

    void updateBarFocus() {
        for (int i = 0; i < (int)barCharts.size(); i++) {
            barCharts[i]->setFocused(i == focusedIndex);
        }
    }

    virtual void sendFeature(int idx) {
        const auto &feats = getFeatures();
        if (idx >= (int)feats.size()) return;
        if (idx >= (int)featureValues.size()) return;

        const auto &feat = feats[idx];
        int val = (int)featureValues[idx];

        // Lovense-style command: "Vibrate:N;", "Rotate:N;", etc.
        // Capitalize the type name for the command
        String cmd = feat.type;
        cmd[0] = toupper(cmd[0]);

        // Multi-motor: "Vibrate1:N;" for index > 0
        if (feat.index > 0) {
            cmd += String(feat.index + 1);
        }

        cmd += ":" + String(val) + ";";
        ESP_LOGD(GENERIC_TAG, "Sending: %s", cmd.c_str());
        send("tx", cmd.c_str());
    }

    virtual void sendAllFeatures(bool setToMin) {
        const auto &feats = getFeatures();
        for (size_t i = 0; i < feats.size() && i < featureValues.size(); i++) {
            if (setToMin) {
                featureValues[i] = feats[i].minValue;
            }
            sendFeature(i);
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    void loadDisplayName() {
        JsonDocument doc;
        if (!readJsonFile(configFileName, doc)) return;

        JsonObject defaults = doc["defaults"];
        if (!defaults.isNull() && !defaults["name"].isNull()) {
            deviceName = defaults["name"].as<String>();
        }
    }
};

#endif  // GENERIC_BUTTPLUGIO_DEVICE_HPP
