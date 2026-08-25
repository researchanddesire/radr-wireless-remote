#ifndef LOVENSE_DEVICE_HPP
#define LOVENSE_DEVICE_HPP

#include <Arduino.h>

#include <components/EncoderDial.h>
#include <components/LinearRailGraph.h>
#include <components/TextButton.h>
#include <constants/Sizes.h>
#include <devices/device.h>

#include "data.hpp"
#include "devices/buttplugio/buttplugIOProtocol.hpp"
#include "services/display.h"
#include "services/encoder.h"

class LovenseDevice : public Device, public ButtplugIoProtocol {
  public:
    LovenseDevice(const NimBLEAdvertisedDevice *advertisedDevice,
                  const String &configFileName,
                  const JsonObjectConst &characteristicsConfig)
        : Device(advertisedDevice),
          ButtplugIoProtocol(configFileName, characteristicsConfig) {
        ESP_LOGI("LOVENSE", "LovenseDevice constructor");
        // We assume these characteristics are present.

        // print the characteristics config
        String characteristicsConfigString = "";
        serializeJson(config, characteristicsConfigString);
        ESP_LOGI("LOVENSE", "Characteristics config: %s",
                 characteristicsConfigString.c_str());

        String tx = config["tx"].as<String>();
        String rx = config["rx"].as<String>();

        ESP_LOGI("LOVENSE", "tx: %s", tx.c_str());
        ESP_LOGI("LOVENSE", "rx: %s", rx.c_str());

        characteristics = {
            {"tx", {NimBLEUUID(tx.c_str())}},
            {"rx",
             DeviceCharacteristics{
                 NimBLEUUID(rx.c_str()),
                 nullptr,
                 nullptr,
                 [this](NimBLERemoteCharacteristic *pRemoteCharacteristic,
                        uint8_t *pData, size_t length, bool isNotify) {
                     rxValue = String(reinterpret_cast<char *>(pData), length);
                     ESP_LOGD("LOVENSE", "Notification received, value: %s",
                              rxValue.c_str());
                 }}}};
    }

    String rxValue = "";
    float vibrateIntensity = 0;
    int leftFocusedIndex = 0;

    String getIdentifier() override {
        rxValue = "";
        // This is required to parse the config file.
        // Resend "DeviceType;" every 250ms until rxValue is set
        const TickType_t checkInterval = 250 / portTICK_PERIOD_MS;
        TickType_t lastSendTick = xTaskGetTickCount();

        do {
            vTaskDelay(
                50 /
                portTICK_PERIOD_MS);  // check frequently for rxValue updates

            // Every 250ms, resend the command if rxValue not set
            TickType_t currentTick = xTaskGetTickCount();
            if ((currentTick - lastSendTick) >= checkInterval) {
                send("tx", "DeviceType;");
                lastSendTick = currentTick;
            }

        } while (rxValue.isEmpty());

        auto deviceType = rxValue;
        String firstLetter = deviceType;
        int colonIndex = deviceType.indexOf(':');
        if (colonIndex != -1) {
            firstLetter = deviceType.substring(0, colonIndex);
        } else {
            firstLetter = deviceType.substring(0, 1);
        }

        return firstLetter;
    }

    void onConnect() override {
        if (protocol.isNull()) {
            String identifier = getIdentifier();
            setProtocol(identifier);
        }

        setVibrate(0);
        isConnected = true;
    }

    NimBLEUUID getServiceUUID() override {
        return advertisedDevice->getServiceUUID();
    }

    void drawControls() override {
        leftEncoder.setBoundaries(0, 16);
        leftEncoder.setAcceleration(0);
        leftEncoder.setEncoderValue(this->vibrateIntensity);

        std::map<String, float *> leftParams = {
            {"Vibrate", &this->vibrateIntensity}};
        draw<EncoderDial>(
            EncoderDial::Props{.encoder = &leftEncoder,
                               .parameters = leftParams,
                               .focusedIndex = &this->leftFocusedIndex,
                               .minValue = 0,
                               .maxValue = 16});

        draw<TextButton>("STOP", pins::BTN_UNDER_C, DISPLAY_WIDTH / 2 - 60,
                         DISPLAY_HEIGHT - 30, 120);
    }

    // helper functions
    bool setVibrate(int intensity) {
        intensity = constrain(intensity, 0, 16);
        String intensityStr = String(intensity);
        return send("tx", String("Vibrate:" + intensityStr + ";").c_str());
    }

    bool setPowerOff() {
        isConnected = false;
        return send("tx", "PowerOff;");
    }

    void onLeftEncoderChange(int value) override { setVibrate(value); }

    void onPause() override {
        setVibrate(0);
        vTaskDelay(250 / portTICK_PERIOD_MS);
        setVibrate(0);
    }

    const char *getName() override { return "Lovense"; }
};

#endif  // LOVENSE_DEVICE_HPP
