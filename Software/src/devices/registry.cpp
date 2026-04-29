#include "registry.h"
#include <Arduino.h>

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "buttplugio/buttplugIOFactory.h"
#include "kinkymakers/ossm/advancedPenetration.hpp"
#include "lovense/LovenseDevice.hpp"
#include "lovense/data.hpp"
#include "lovense/domi/domi_device.hpp"
#include "researchAndDesire/ossm/ossm_device.hpp"
#include "serviceUUIDs.h"

static const char *REGISTRY_TAG = "REGISTRY";

// Define the global registry
std::unordered_map<std::string, DeviceFactory> registry;
std::unordered_map<std::string, std::string> registryNames;

void initRegistry() {
    // Clear any existing entries
    registry.clear();
    registryNames.clear();

    // Known explicit services
    registry.try_emplace(OSSM_SERVICE_ID,
                         [](const NimBLEAdvertisedDevice *advertisedDevice) -> Device * { return new OSSM(advertisedDevice); });
    registry.try_emplace(OSSM_ADVANCED_SERVICE_ID,
                         [](const NimBLEAdvertisedDevice *advertisedDevice) -> Device * { return new OSSMAdvanced(advertisedDevice); });
    registryNames.try_emplace(OSSM_ADVANCED_SERVICE_ID, OSSM_ADVANCED_SERVICE_NAME);

    // Try to read registry.json from LittleFS
    if (LittleFS.exists("/registry.json")) {
        ESP_LOGI(REGISTRY_TAG, "registry.json exists");
        vTaskDelay(1);
        File file = LittleFS.open("/registry.json", "r");
        vTaskDelay(1);
        if (file) {
            size_t fileSize = file.size();
            if (fileSize > 0) {
                vTaskDelay(1);
                String jsonString = file.readString();
                file.close();

                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, jsonString);
                vTaskDelay(1);

                if (!error) {
                    ESP_LOGI(REGISTRY_TAG, "Loaded device registry from LittleFS");

                    // Parse JSON and add UUIDs to registry
                    for (JsonPair pair : doc.as<JsonObject>()) {
                        std::string uuidStr = pair.key().c_str();
                        ESP_LOGD(REGISTRY_TAG, "UUID: %s", uuidStr.c_str());
                        std::transform(uuidStr.begin(), uuidStr.end(), uuidStr.begin(), ::toupper);
                        ESP_LOGD(REGISTRY_TAG, "Uppercase UUID: %s", uuidStr.c_str());

                        registry.emplace(uuidStr, ButtplugIODeviceFactory);
                        vTaskDelay(1);
                    }
                } else {
                    ESP_LOGW(REGISTRY_TAG, "Failed to parse registry.json: %s", error.c_str());
                }
            } else {
                ESP_LOGW(REGISTRY_TAG, "registry.json file too large or empty");
                file.close();
            }
        } else {
            ESP_LOGW(REGISTRY_TAG, "Failed to open registry.json");
        }
    } else {
        ESP_LOGD(REGISTRY_TAG, "registry.json not found in LittleFS");
    }
}

const DeviceFactory *getDeviceFactory(const NimBLEUUID &serviceUUID) {
    ESP_LOGI(REGISTRY_TAG, "Getting device factory for service UUID: %s", serviceUUID.toString().c_str());
    // Convert NimBLEUUID to Uppercase std::string for lookup
    std::string uuidStr = serviceUUID.toString().c_str();
    std::transform(uuidStr.begin(), uuidStr.end(), uuidStr.begin(), ::toupper);

    auto it = registry.find(uuidStr);

    if (it == registry.end()) {
        // TODO: Manage this better. Send the user to an error screen.
        ESP_LOGI(REGISTRY_TAG, "No device factory found for service UUID: %s", serviceUUID.toString().c_str());
        return nullptr;
    }

    return &it->second;
}

const std::string getDeviceName(const NimBLEUUID &serviceUUID, std::string defaultName) {
    ESP_LOGI(REGISTRY_TAG, "Getting device name for service UUID: %s", serviceUUID.toString().c_str());
    std::string uuidStr = serviceUUID.toString().c_str();
    std::transform(uuidStr.begin(), uuidStr.end(), uuidStr.begin(), ::toupper);

    auto it = registryNames.find(uuidStr);

    if (it == registryNames.end()) {
        ESP_LOGI(REGISTRY_TAG, "No device name found for service UUID: %s", serviceUUID.toString().c_str());
        return defaultName;
    }

    return defaultName + "-" + it->second;
}
