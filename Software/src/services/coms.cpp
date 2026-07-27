#include "coms.h"

/** NimBLE_Client Demo:
 *
 *  Demonstrates many of the available features of the NimBLE client library.
 *
 *  Created: on March 24 2020
 *      Author: H2zero
 */

#include <Arduino.h>

#include <NimBLEDevice.h>
#include <esp_log.h>
#include <queue>
#include <regex>

#include "rad_ble.h"

static const char *TAG_COMS = "COMS";

static const NimBLEAdvertisedDevice *advDevice;
static bool doConnect = false;
static uint32_t scanTimeMs =
    0; /** scan time in milliseconds, 0 = scan forever */

static std::queue<String> commandQueue;
static std::vector<DiscoveredDevice> discoveredDevices;

struct ScanMonitorRequest {
    int timeoutMs;
    void (*callback)();
    uint32_t generation;
};

static volatile uint32_t scanGeneration = 0;

class PeripheralCallbacks final : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *, NimBLEConnInfo &connection) override {
        radBleServer.onConnect(connection.getConnHandle());
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connection,
                      int) override {
        radBleServer.onDisconnect(connection.getConnHandle());
        server->startAdvertising();
    }
} peripheralCallbacks;

/** Define a class to handle the callbacks when scan events are received */
class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
        // get all service UUIDs
        const DeviceFactory *factory = nullptr;
        auto countOfServiceUUIDs = advertisedDevice->getServiceUUIDCount();
        for (int i = 0; i < countOfServiceUUIDs; i++) {
            vTaskDelay(1);
            auto serviceUUID = advertisedDevice->getServiceUUID(i);

            auto name = advertisedDevice->getName();

            // print the service UUID and name
            ESP_LOGI("DEBUG_FOLLOWER", "Service UUID: %s, Name: %s",
                     serviceUUID.toString().c_str(), name.c_str());

            // check if the service UUID is in the registry
            factory = getDeviceFactory(serviceUUID);

            if (factory != nullptr) {
                ESP_LOGI("DEBUG_FOLLOWER", "Found factory for service UUID: %s",
                         serviceUUID.toString().c_str());
                break;
            }
        }

        if (!factory) {
            return;
        }

        // Check if device already exists in list (by address)
        std::string address = advertisedDevice->getAddress().toString();
        for (auto &dev : discoveredDevices) {
            if (dev.advertisedDevice->getAddress().toString() == address) {
                // Update RSSI if device already in list
                dev.rssi = advertisedDevice->getRSSI();
                return;
            }
        }

        // Add new device to list
        DiscoveredDevice newDevice;
        newDevice.advertisedDevice = advertisedDevice;
        newDevice.factory = factory;
        newDevice.name = advertisedDevice->getName().c_str();
        newDevice.rssi = advertisedDevice->getRSSI();
        discoveredDevices.push_back(newDevice);

        ESP_LOGI(TAG_COMS, "Found device: %s (RSSI: %d)",
                 newDevice.name.c_str(), newDevice.rssi);

        return;
    }

    void onScanEnd(const NimBLEScanResults &results, int reason) override {
        ESP_LOGI(TAG_COMS, "Scan ended. Found %d devices",
                 discoveredDevices.size());
    }
} scanCallbacks;

/** Notification / Indication receiving handler callback */
void notifyCB(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
              size_t length, bool isNotify) {
    std::string str = (isNotify == true) ? "Notification" : "Indication";
    str += " from ";
    str += pRemoteCharacteristic->getClient()->getPeerAddress().toString();
    str += ": Service = " +
           pRemoteCharacteristic->getRemoteService()->getUUID().toString();
    str += ", Characteristic = " + pRemoteCharacteristic->getUUID().toString();
    str += ", Value = " + std::string((char *)pData, length);

    ESP_LOGV(TAG_COMS, "%s", str.c_str());
}

/** Handles the provisioning of clients and connects / interfaces with the
 * server */
bool connectToServer() {
    if (advDevice == nullptr) {
        ESP_LOGE(TAG_COMS, "connectToServer called with null advDevice");
        return false;
    }

    return true;
}

void sendCommand(const String &command) {
    // Only allow commands of the form:
    // set:depth|sensation|pattern|speed|stroke:0-100 Example: set:speed:75

    // Regex: ^set:(depth|sensation|pattern|speed|stroke):([0-9]{1,3})$
    // Value must be 0-100

    std::regex cmdRegex(
        "^set:(depth|sensation|pattern|speed|stroke):([0-9]{1,3})$");
    std::cmatch match;
    if (std::regex_match(command.c_str(), match, cmdRegex)) {
        int value = atoi(match[2].str().c_str());
        if (value >= 0 && value <= 100) {
            std::string cmdType = match[1].str();

            // Remove any previous commands of the same type from the queue
            std::queue<String> tempQueue;
            while (!commandQueue.empty()) {
                String queuedCmd = commandQueue.front();
                commandQueue.pop();

                std::cmatch queuedMatch;
                if (std::regex_match(queuedCmd.c_str(), queuedMatch,
                                     cmdRegex)) {
                    std::string queuedType = queuedMatch[1].str();
                    if (queuedType == cmdType) {
                        // Skip this command (removes previous of same type)
                        continue;
                    }
                }
                tempQueue.push(queuedCmd);
            }
            // Restore the filtered queue
            commandQueue = tempQueue;

            // Add the new command
            commandQueue.push(command);
            return;
        }
    }
    ESP_LOGW(TAG_COMS, "Invalid command format: %s", command.c_str());
}

void initBLE() {
    ESP_LOGI(TAG_COMS, "Starting NimBLE Client");

    /** Initialize NimBLE and set the device name */
    static String deviceName = radble::loadDeviceName("RADR");
    NimBLEDevice::init(deviceName.c_str());
    NimBLEDevice::setMTU(512);

    /** Set the transmit power to maximum (9 dBm for ESP32) */
    NimBLEDevice::setPower(9); /** 9dBm */
    NimBLEScan *pScan = NimBLEDevice::getScan();

    /** Set the callbacks to call when scan events occur, no duplicates */
    pScan->setScanCallbacks(&scanCallbacks, false);

    /** Set scan interval (how often) and window (how long) in milliseconds */
    pScan->setInterval(100);
    pScan->setWindow(100);

    /**
     * Active scan will gather scan response data from advertisers
     *  but will use more energy from both devices
     */
    pScan->setActiveScan(true);

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(&peripheralCallbacks);
    if (initRadBle(server)) {
        NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
        advertising->setName(deviceName.c_str());
        advertising->addServiceUUID(radBleServer.serviceUuid());
        advertising->enableScanResponse(true);
        advertising->start();
        ESP_LOGI(TAG_COMS, "RAD BLE peripheral advertising started");
    } else {
        ESP_LOGE(TAG_COMS, "RAD BLE peripheral initialization failed");
    }

    /** Start scanning for advertisers */
    // pScan->start(scanTimeMs);
    ESP_LOGI(TAG_COMS, "Scanning for peripherals");
}

std::vector<DiscoveredDevice> &getDiscoveredDevices() {
    return discoveredDevices;
}

void clearDiscoveredDevices() {
    discoveredDevices.clear();
    ESP_LOGI(TAG_COMS, "Cleared discovered devices list");
}

void connectToDiscoveredDevice(int index) {
    if (index < 0 || index >= discoveredDevices.size()) {
        ESP_LOGE(TAG_COMS, "Invalid device index: %d", index);
        return;
    }

    const DiscoveredDevice &selectedDevice = discoveredDevices[index];
    ESP_LOGI(TAG_COMS, "Connecting to device: %s", selectedDevice.name.c_str());

    // Stop scanning
    NimBLEDevice::getScan()->stop();

    // Create device instance
    advDevice = selectedDevice.advertisedDevice;
    device = (*selectedDevice.factory)(selectedDevice.advertisedDevice);
}

bool startScanWithTimeout(int timeoutMs, void (*onComplete)()) {
    clearDiscoveredDevices();
    NimBLEScan *pScan = NimBLEDevice::getScan();
    const uint32_t generation = ++scanGeneration;
    if (pScan->isScanning()) pScan->stop();
    if (!pScan->start(0)) {
        ESP_LOGE(TAG_COMS, "Could not start BLE scan");
        return false;
    }

    // Start a task to monitor scanning and call callback
    // NOTE: Stack size must be large enough to handle the callback chain,
    // which includes state machine processing and UI operations
    auto *request = new ScanMonitorRequest{timeoutMs, onComplete, generation};
    if (xTaskCreate(
        [](void *param) {
            auto *request = static_cast<ScanMonitorRequest *>(param);

            // Wait for scan timeout
            vTaskDelay(pdMS_TO_TICKS(request->timeoutMs));

            // A newer request owns the scanner and its completion callback.
            if (request->generation != scanGeneration) {
                delete request;
                vTaskDelete(NULL);
                return;
            }

            // Stop scanning
            NimBLEScan *pScan = NimBLEDevice::getScan();
            if (pScan->isScanning()) {
                pScan->stop();
            }

            // Call completion callback
            if (request->callback) {
                request->callback();
            }

            delete request;
            vTaskDelete(NULL);
        },
        "scanMonitor", 8192, request, 1, NULL) != pdPASS) {
        delete request;
        ++scanGeneration;
        pScan->stop();
        ESP_LOGE(TAG_COMS, "Could not create BLE scan monitor task");
        return false;
    }
    return true;
}
