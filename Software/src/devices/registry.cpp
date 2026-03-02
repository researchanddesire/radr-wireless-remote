#include "registry.h"
#include <Arduino.h>

#include <ArduinoJson.h>

#include "buttplugio/buttplugIOFactory.h"
#include "registry_data.h"
#include "researchAndDesire/ossm/ossm_device.hpp"
#include "serviceUUIDs.h"

static const char *REGISTRY_TAG = "REGISTRY";

// Explicit device factories for special devices
static std::unordered_map<std::string, DeviceFactory> explicitRegistry;

// Parsed registry document (parsed once from embedded string)
static JsonDocument registryDoc;
static bool registryLoaded = false;

// Cache for factories resolved from registry (need stable addresses for
// pointers)
static std::unordered_map<std::string, DeviceFactory> factoryCache;

void initRegistry() {
    explicitRegistry.clear();
    factoryCache.clear();

    // Known explicit services (special handling, not from registry)
    explicitRegistry.emplace(
        OSSM_SERVICE_ID,
        [](const NimBLEAdvertisedDevice *advertisedDevice) -> Device * {
            return new OSSM(advertisedDevice);
        });

    // Domi 2 previously had an explicit factory here. It now goes through
    // the generic ButtplugIO path and gets a LovenseGeneric instance, which
    // supports all Lovense control modes. To re-add a device-specific override:
    //
    // #include "lovense/domi/domi_device.hpp"
    // explicitRegistry.emplace(
    //     DOMI_SERVICE_ID,
    //     [](const NimBLEAdvertisedDevice *advertisedDevice) -> Device * {
    //         return new Domi2(advertisedDevice);
    //     });

    // Parse embedded registry (no file I/O)
    DeserializationError error = deserializeJson(registryDoc, REGISTRY_JSON);
    if (!error) {
        registryLoaded = true;
        ESP_LOGI(REGISTRY_TAG, "Registry loaded (%d entries)",
                 registryDoc.as<JsonObject>().size());
    } else {
        ESP_LOGE(REGISTRY_TAG, "Failed to parse embedded registry: %s",
                 error.c_str());
    }
}

const DeviceFactory *getDeviceFactory(const NimBLEUUID &serviceUUID) {
    ESP_LOGI(REGISTRY_TAG, "Looking up factory for: %s",
             serviceUUID.toString().c_str());

    std::string uuidStr = serviceUUID.toString().c_str();
    std::transform(uuidStr.begin(), uuidStr.end(), uuidStr.begin(), ::toupper);

    // 1. Check explicit registry first (special devices like OSSM)
    auto explicitIt = explicitRegistry.find(uuidStr);
    if (explicitIt != explicitRegistry.end()) {
        ESP_LOGD(REGISTRY_TAG, "Found in explicit registry");
        return &explicitIt->second;
    }

    // 2. Check if already cached from JSON lookup
    auto cacheIt = factoryCache.find(uuidStr);
    if (cacheIt != factoryCache.end()) {
        ESP_LOGD(REGISTRY_TAG, "Found in cache");
        return &cacheIt->second;
    }

    // 3. Lazy lookup in JSON document
    if (registryLoaded) {
        std::string lowerUuid = uuidStr;
        std::transform(lowerUuid.begin(), lowerUuid.end(), lowerUuid.begin(),
                       ::tolower);

        if (registryDoc.containsKey(lowerUuid)) {
            ESP_LOGD(REGISTRY_TAG, "Found in JSON, caching");
            auto [it, _] =
                factoryCache.emplace(uuidStr, ButtplugIODeviceFactory);
            return &it->second;
        }
    }

    ESP_LOGI(REGISTRY_TAG, "No factory found for: %s", uuidStr.c_str());
    return nullptr;
}
