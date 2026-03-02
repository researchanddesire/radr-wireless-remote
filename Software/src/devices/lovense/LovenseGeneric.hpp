#ifndef LOVENSE_GENERIC_HPP
#define LOVENSE_GENERIC_HPP

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

#include "devices/buttplugio/GenericButtplugIODevice.hpp"

static const char *LOVENSE_TAG = "LOVENSE_GEN";

// Mirrors the 6 handler categories from the buttplug Rust library.
// Selected after DeviceType; identification + feature analysis.
enum class LovenseStrategy {
    Single,         // 1 output: Vibrate:N;
    Dual,           // 2 vibrators: Vibrate1:N; / Vibrate2:N;
    Mply,           // 3+ outputs: Mply:v1:v2:v3;
    RotateVibrate,  // 1 vibrate + 1 rotate: Vibrate:N; + RotateChange; + Rotate:N;
    Max,            // 1 vibrate + 1 constrict: Vibrate:N; + Air:Level:N;
    Stroker,        // Solace/Solace Pro: Mply:speed:range; + FSetSite:pos;
};

class LovenseGeneric : public GenericButtplugIODevice {
  public:
    LovenseGeneric(const NimBLEAdvertisedDevice *advertisedDevice,
                   const String &configFileName,
                   const JsonObjectConst &characteristicsConfig)
        : GenericButtplugIODevice(advertisedDevice, configFileName,
                                  characteristicsConfig) {
        ESP_LOGI(LOVENSE_TAG, "LovenseGeneric constructor");
    }

    ~LovenseGeneric() override { stopStrokerTask(); }

    void onConnect() override {
        if (protocol.isNull()) {
            String identifier = getIdentifier();
            deviceIdentifier = identifier;
            if (identifier.isEmpty()) {
                setProtocolFromDefaults();
            } else {
                setProtocol(identifier);
            }
        }

        resolveStrategy();

        initFeatureValues();
        sendAllFeatures(true);

        if (strategy == LovenseStrategy::Stroker) {
            startStrokerTask();
        }

        isConnected = true;
    }

    void onDisconnect() override { stopStrokerTask(); }

    const char *getName() override {
        return getDeviceName().length() > 0 ? getDeviceName().c_str()
                                            : "Lovense";
    }

  protected:
    void sendFeature(int idx) override {
        const auto &feats = getFeatures();
        if (idx >= (int)feats.size()) return;
        if (idx >= (int)featureValues.size()) return;

        const auto &feat = feats[idx];
        int val = (int)featureValues[idx];

        switch (strategy) {
            case LovenseStrategy::Single:
                sendSingle(feat, val);
                break;
            case LovenseStrategy::Dual:
                sendDual(feat, val);
                break;
            case LovenseStrategy::Mply:
                sendMplyPacket();
                break;
            case LovenseStrategy::RotateVibrate:
                sendRotateVibrate(feat, val);
                break;
            case LovenseStrategy::Max:
                sendMax(feat, val);
                break;
            case LovenseStrategy::Stroker:
                sendStroker(feat, val);
                break;
        }
    }

    void sendAllFeatures(bool setToMin) override {
        const auto &feats = getFeatures();
        for (size_t i = 0; i < feats.size() && i < featureValues.size(); i++) {
            if (setToMin) {
                featureValues[i] = feats[i].minValue;
            }
        }

        if (strategy == LovenseStrategy::Mply) {
            sendMplyPacket();
        } else if (strategy == LovenseStrategy::Stroker) {
            sendStrokerStop();
        } else {
            for (size_t i = 0; i < feats.size() && i < featureValues.size();
                 i++) {
                sendFeature(i);
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
        }
    }

  private:
    LovenseStrategy strategy = LovenseStrategy::Single;
    String deviceIdentifier = "";
    bool lastRotatePositive = true;

    // Stroker background task state
    TaskHandle_t strokerTaskHandle = nullptr;
    std::atomic<int32_t> strokerGoalPosition{0};
    std::atomic<uint32_t> strokerDuration{0};
    bool needRangeZeroed = false;  // Solace (H) needs range=0 when stopped

    // ── Strategy resolver ───────────────────────────────────────────────
    // Mirrors buttplug Rust mod.rs LovenseInitializer::initialize()
    void resolveStrategy() {
        const auto &feats = getFeatures();

        if (deviceIdentifier == "BA" || deviceIdentifier == "H") {
            strategy = LovenseStrategy::Stroker;
            needRangeZeroed = (deviceIdentifier == "H");
            ESP_LOGI(LOVENSE_TAG, "Strategy: Stroker (id=%s)",
                     deviceIdentifier.c_str());
            return;
        }

        int outputCount = feats.size();
        int vibrateCount = 0;
        int rotateCount = 0;
        int constrictCount = 0;
        int oscillateCount = 0;

        for (const auto &f : feats) {
            if (f.type == "vibrate") vibrateCount++;
            else if (f.type == "rotate") rotateCount++;
            else if (f.type == "constrict") constrictCount++;
            else if (f.type == "oscillate") oscillateCount++;
        }

        int vibratorLike = vibrateCount + oscillateCount;

        if (outputCount == 1) {
            strategy = LovenseStrategy::Single;
        } else if (outputCount == 2 && vibrateCount == 1 &&
                   constrictCount == 1) {
            strategy = LovenseStrategy::Max;
        } else if (outputCount == 2 && vibrateCount == 1 &&
                   rotateCount == 1) {
            strategy = LovenseStrategy::RotateVibrate;
        } else if ((vibratorLike == 2 && outputCount > 2) ||
                   vibratorLike > 2) {
            strategy = LovenseStrategy::Mply;
        } else {
            strategy = LovenseStrategy::Dual;
        }

        ESP_LOGI(LOVENSE_TAG, "Strategy: %d (outputs=%d, vib=%d, rot=%d, "
                 "constrict=%d, osc=%d)",
                 (int)strategy, outputCount, vibrateCount, rotateCount,
                 constrictCount, oscillateCount);
    }

    // ── Single actuator ─────────────────────────────────────────────────
    // Vibrate and oscillate both map to Vibrate:N; for Lovense
    void sendSingle(const ButtplugFeature &feat, int val) {
        String cmd = "Vibrate:" + String(val) + ";";
        ESP_LOGD(LOVENSE_TAG, "Single: %s", cmd.c_str());
        send("tx", cmd.c_str());
    }

    // ── Dual actuator ───────────────────────────────────────────────────
    // Vibrate1:N; / Vibrate2:N; (1-indexed)
    void sendDual(const ButtplugFeature &feat, int val) {
        String cmd =
            "Vibrate" + String(feat.index + 1) + ":" + String(val) + ";";
        ESP_LOGD(LOVENSE_TAG, "Dual: %s", cmd.c_str());
        send("tx", cmd.c_str());
    }

    // ── Multi-actuator (Mply) ───────────────────────────────────────────
    // Sends all feature values atomically as Mply:v1:v2:v3:...;
    void sendMplyPacket() {
        String cmd = "Mply:";
        for (size_t i = 0; i < featureValues.size(); i++) {
            if (i > 0) cmd += ":";
            int val = (int)featureValues[i];
            const auto &feats = getFeatures();
            if (i < feats.size() && feats[i].type == "rotate") {
                val = abs(val);
            }
            cmd += String(val);
        }
        cmd += ";";
        ESP_LOGD(LOVENSE_TAG, "Mply: %s", cmd.c_str());
        send("tx", cmd.c_str());
    }

    // ── Rotate + Vibrate ────────────────────────────────────────────────
    // Vibrate features: Vibrate:N;
    // Rotate features: RotateChange; on direction flip, then Rotate:abs(N);
    void sendRotateVibrate(const ButtplugFeature &feat, int val) {
        if (feat.type == "vibrate" || feat.type == "oscillate") {
            String cmd = "Vibrate:" + String(val) + ";";
            ESP_LOGD(LOVENSE_TAG, "RotVib vibrate: %s", cmd.c_str());
            send("tx", cmd.c_str());
        } else if (feat.type == "rotate") {
            bool positive = (val >= 0);
            if (positive != lastRotatePositive) {
                ESP_LOGD(LOVENSE_TAG, "RotVib: direction change");
                send("tx", "RotateChange;");
                vTaskDelay(10 / portTICK_PERIOD_MS);
                lastRotatePositive = positive;
            }
            String cmd = "Rotate:" + String(abs(val)) + ";";
            ESP_LOGD(LOVENSE_TAG, "RotVib rotate: %s", cmd.c_str());
            send("tx", cmd.c_str());
        }
    }

    // ── Max (vibrate + constrict/air pump) ──────────────────────────────
    void sendMax(const ButtplugFeature &feat, int val) {
        if (feat.type == "vibrate" || feat.type == "oscillate") {
            String cmd = "Vibrate:" + String(val) + ";";
            ESP_LOGD(LOVENSE_TAG, "Max vibrate: %s", cmd.c_str());
            send("tx", cmd.c_str());
        } else if (feat.type == "constrict") {
            String cmd = "Air:Level:" + String(val) + ";";
            ESP_LOGD(LOVENSE_TAG, "Max constrict: %s", cmd.c_str());
            send("tx", cmd.c_str());
        }
    }

    // ── Stroker (Solace / Solace Pro) ───────────────────────────────────
    // Oscillate: Mply:speed:range; (range=20 when active)
    // Position: updates atomic goal for background interpolation task
    void sendStroker(const ButtplugFeature &feat, int val) {
        if (feat.type == "oscillate") {
            int range = (val == 0 && needRangeZeroed) ? 0 : 20;
            String cmd =
                "Mply:" + String(val) + ":" + String(range) + ";";
            ESP_LOGD(LOVENSE_TAG, "Stroker oscillate: %s", cmd.c_str());
            send("tx", cmd.c_str());
        } else if (feat.type == "position_with_duration") {
            strokerGoalPosition.store(val, std::memory_order_relaxed);
            // Duration comes from the feature's max range; for UI knob control
            // we use a fixed smooth interpolation rate
            strokerDuration.store(500, std::memory_order_relaxed);
        }
    }

    void sendStrokerStop() {
        strokerGoalPosition.store(0, std::memory_order_relaxed);
        int range = needRangeZeroed ? 0 : 20;
        String cmd = "Mply:0:" + String(range) + ";";
        send("tx", cmd.c_str());
    }

    // ── Stroker background task ─────────────────────────────────────────
    // Interpolates position and sends FSetSite:pos; every 100ms,
    // matching buttplug's update_linear_movement in lovense_stroker.rs
    void startStrokerTask() {
        if (strokerTaskHandle != nullptr) return;
        xTaskCreatePinnedToCore(strokerTaskEntry, "lovense_stroker", 4096,
                                this, 1, &strokerTaskHandle, 0);
    }

    void stopStrokerTask() {
        if (strokerTaskHandle != nullptr) {
            vTaskDelete(strokerTaskHandle);
            strokerTaskHandle = nullptr;
        }
    }

    static void strokerTaskEntry(void *param) {
        auto *self = static_cast<LovenseGeneric *>(param);
        self->strokerLoop();
    }

    void strokerLoop() {
        int32_t lastGoalPosition = 0;
        int32_t currentMoveAmount = 0;
        int32_t currentPosition = 0;

        while (true) {
            int32_t goalPosition =
                strokerGoalPosition.load(std::memory_order_relaxed);

            if (lastGoalPosition != goalPosition) {
                lastGoalPosition = goalPosition;
                uint32_t dur =
                    strokerDuration.load(std::memory_order_relaxed);
                int32_t moveSteps = max((int32_t)(dur / 100), (int32_t)1);
                currentMoveAmount =
                    (goalPosition - currentPosition) / moveSteps;
            }

            if (currentPosition == lastGoalPosition) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }

            currentPosition += currentMoveAmount;

            // Clamp to goal to prevent overshoot
            if (currentMoveAmount < 0) {
                if (currentPosition < lastGoalPosition)
                    currentPosition = lastGoalPosition;
            } else {
                if (currentPosition > lastGoalPosition)
                    currentPosition = lastGoalPosition;
            }

            String cmd = "FSetSite:" + String(currentPosition) + ";";
            send("tx", cmd.c_str());

            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
};

#endif  // LOVENSE_GENERIC_HPP
