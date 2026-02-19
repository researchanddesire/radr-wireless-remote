#ifndef BUTTPLUGIO_PROTOCOL_HPP
#define BUTTPLUGIO_PROTOCOL_HPP

#include <ArduinoJson.h>
#include <NimBLEUUID.h>

#include "utils.h"

struct ButtplugFeature {
    String name = "";
    String type = "";
    int minValue = 0;
    int maxValue = 100;
    int index = 0;
};

class ButtplugIoProtocol {
  protected:
    NimBLEUUID serviceUUID = NimBLEUUID();
    JsonDocument config;
    JsonDocument protocol;
    String configFileName = "";
    String deviceType = "";
    String deviceName = "";
    std::vector<ButtplugFeature> features = {};

  public:
    ButtplugIoProtocol(const String& configFileName,
                       const JsonObjectConst& characteristicsConfig)
        : configFileName(configFileName) {
        config.set(characteristicsConfig);
    }

    virtual String getIdentifier() = 0;

    const std::vector<ButtplugFeature>& getFeatures() const {
        return features;
    }

    const String& getDeviceName() const { return deviceName; }

    void setProtocol(const String& identifierString) {
        JsonDocument doc;
        if (!readJsonFile(configFileName, doc)) {
            ESP_LOGE("BUTTPLUGIO_PROTOCOL", "Failed to read config file: %s",
                     configFileName.c_str());
            return;
        }

        JsonObject defaults = doc["defaults"];
        if (defaults.isNull()) {
            ESP_LOGE("BUTTPLUGIO_PROTOCOL", "Defaults not found");
            return;
        }

        deviceName = defaults["name"] | "Unknown";

        JsonArray featuresJson = JsonArray();
        bool found = false;

        JsonArray configurations = doc["configurations"];
        if (!configurations.isNull()) {
            for (JsonObject configuration : configurations) {
                JsonArray identifierArray = configuration["identifier"];
                for (String item : identifierArray) {
                    if (item.compareTo(identifierString) == 0) {
                        ESP_LOGI("BUTTPLUGIO_PROTOCOL",
                                 "Found configuration:");
                        featuresJson = configuration["features"];

                        if (!configuration["name"].isNull()) {
                            deviceName = configuration["name"].as<String>();
                        }

                        if (featuresJson.isNull()) {
                            featuresJson = defaults["features"];
                        }

                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
        }

        if (!found) {
            ESP_LOGI("BUTTPLUGIO_PROTOCOL",
                     "No config match for '%s', using defaults",
                     identifierString.c_str());
            featuresJson = defaults["features"];
        }

        if (featuresJson.isNull()) {
            ESP_LOGE("BUTTPLUGIO_PROTOCOL", "Features not found: %s",
                     identifierString.c_str());
            return;
        }

        protocol.set(featuresJson);

        String featuresString = "";
        serializeJson(featuresJson, featuresString);
        ESP_LOGI("BUTTPLUGIO_PROTOCOL", "Features: %s", featuresString.c_str());

        parseFeatures(featuresJson);
    }

    void setProtocolFromDefaults() {
        JsonDocument doc;
        if (!readJsonFile(configFileName, doc)) {
            ESP_LOGE("BUTTPLUGIO_PROTOCOL", "Failed to read config file: %s",
                     configFileName.c_str());
            return;
        }

        JsonObject defaults = doc["defaults"];
        if (defaults.isNull()) {
            ESP_LOGE("BUTTPLUGIO_PROTOCOL", "Defaults not found");
            return;
        }

        deviceName = defaults["name"] | "Unknown";
        JsonArray featuresJson = defaults["features"];
        if (featuresJson.isNull()) {
            ESP_LOGE("BUTTPLUGIO_PROTOCOL", "Default features not found");
            return;
        }

        protocol.set(featuresJson);
        parseFeatures(featuresJson);
    }

  private:
    void parseFeatures(const JsonArray& featuresJson) {
        features.clear();
        std::map<String, int> typeCounters;

        for (JsonObject feat : featuresJson) {
            JsonObject output = feat["output"];
            if (output.isNull()) continue;

            for (JsonPair kv : output) {
                String type = kv.key().c_str();
                JsonObject featureData = kv.value();

                int minVal = 0;
                int maxVal = 100;

                JsonArray range = featureData["value"];
                if (range.isNull()) range = featureData["position"];
                if (!range.isNull()) {
                    minVal = range[0] | 0;
                    maxVal = range[1] | 100;
                }

                int idx = typeCounters[type]++;

                String name = type;
                name[0] = toupper(name[0]);
                if (idx > 0) {
                    name += " " + String(idx + 1);
                }

                String description = feat["description"] | "";
                if (description.length() > 0) {
                    name = description;
                }

                features.push_back({name, type, minVal, maxVal, idx});

                ESP_LOGI("BUTTPLUGIO_PROTOCOL", "Feature: %s [%d-%d] idx=%d",
                         name.c_str(), minVal, maxVal, idx);
            }
        }

        ESP_LOGI("BUTTPLUGIO_PROTOCOL", "Parsed %d output features",
                 features.size());
    }
};

#endif  // BUTTPLUGIO_PROTOCOL_HPP
