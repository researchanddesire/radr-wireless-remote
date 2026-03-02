#include "buttplugIOFactory.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utils.h"

/// @brief A device factory for devices that use the ButtplugIO protocol.
/// @param advertisedDevice The advertised BLE device to create a device for
/// @return A pointer to the created Device, or nullptr if creation failed
Device* ButtplugIODeviceFactory(
    const NimBLEAdvertisedDevice* advertisedDevice) {
    auto serviceUUID = advertisedDevice->getServiceUUID().toString();
    String deviceName = String(advertisedDevice->getName().c_str());

    // Read and parse registry.json
    JsonDocument registryDoc;
    if (!readJsonFile("/registry.json", registryDoc)) {
        return nullptr;
    }
    vTaskDelay(1);

    if (registryDoc[serviceUUID].isNull()) {
        ESP_LOGE("BUTTPLUGIO", "Service UUID not found in registry.json: %s",
                 serviceUUID.c_str());
        return nullptr;
    }

    JsonArrayConst configFiles = registryDoc[serviceUUID];
    if (configFiles.isNull()) {
        ESP_LOGE("BUTTPLUGIO", "Config files not found in registry.json: %s",
                 serviceUUID.c_str());
        return nullptr;
    }

    String configFileName = "";

    // Search through config files to find a matching device
    for (JsonString configFileRow : configFiles) {
        configFileName = configFileRow.c_str();

        JsonDocument configDoc;
        if (!readJsonFile(configFileName, configDoc)) {
            vTaskDelay(1);
            continue;
        }
        vTaskDelay(1);

        ESP_LOGI("BUTTPLUGIO", "Opened config file: %s", configFileName);

        if (!validateConfigStructure(configDoc, configFileName)) {
            vTaskDelay(1);
            continue;
        }
        vTaskDelay(1);

        JsonObjectConst communication = configDoc["communication"][0];
        JsonObjectConst btleData = communication["btle"];
        JsonArrayConst names = btleData["names"];

        if (!matchesDeviceName(deviceName, names)) {
            vTaskDelay(1);
            continue;
        }
        vTaskDelay(1);

        JsonObjectConst characteristics =
            extractCharacteristics(configDoc, serviceUUID);
        vTaskDelay(1);

        if (characteristics.isNull()) {
            vTaskDelay(1);
            continue;
        }

        if (configFileName == "/protocols/lovense.json" ||
            configFileName == "/protocols/lovense-connect-service.json") {
            return new LovenseGeneric(advertisedDevice, configFileName,
                                      characteristics);
        }

        return new GenericButtplugIODevice(advertisedDevice, configFileName,
                                            characteristics);
    }

    ESP_LOGW("BUTTPLUGIO", "No matching configuration found for device: %s",
             deviceName.c_str());
    return nullptr;
}
