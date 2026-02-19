#include "DeviceEmulator.h"

#include <nvs_flash.h>

static const char *TAG = "EMULATOR";

bool DeviceEmulator::init() {
    return loadRegistry(registry);
}

int DeviceEmulator::getDeviceCount() const {
    return registry.size();
}

const DeviceEntry *DeviceEmulator::getCurrentDevice() const {
    if (!deviceLoaded) return nullptr;
    return &currentDevice;
}

int DeviceEmulator::getCurrentIndex() const {
    return currentIndex;
}

bool DeviceEmulator::isConnected() const {
    return connected;
}

void DeviceEmulator::setDisplayCallback(DisplayCallback cb) {
    displayCallback = cb;
}

bool DeviceEmulator::startDevice(int index) {
    if (index < 0 || index >= (int)registry.size()) return false;
    currentIndex = index;
    connected = false;
    deviceLoaded = false;

    ESP_LOGI(TAG, "Loading device %d/%d from %s", index + 1, registry.size(),
             registry[index].configFile.c_str());

    if (!loadDeviceFromRegistry(registry[index], currentDevice)) {
        ESP_LOGW(TAG, "Failed to load device %d, skipping",  index);
        deviceLoaded = false;
        if (displayCallback) displayCallback();
        return false;
    }
    deviceLoaded = true;

    for (auto &feat : currentDevice.features) {
        feat.currentValue = feat.minValue;
    }

    ESP_LOGI(TAG, "Starting emulation: %s (UUID: %s, BLE: %s)",
             currentDevice.deviceName.c_str(),
             currentDevice.serviceUUID.c_str(),
             currentDevice.bleName.c_str());

    // Erase NVS to clear any previously stored BLE device name
    nvs_flash_erase();
    nvs_flash_init();

    NimBLEDevice::init(currentDevice.bleName.c_str());
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(this);

    String svcUUID = currentDevice.serviceUUID;
    svcUUID.toLowerCase();
    NimBLEService *pService = pServer->createService(svcUUID.c_str());

    String txUUID = currentDevice.txUUID;
    txUUID.toLowerCase();
    txChar = pService->createCharacteristic(
        txUUID.c_str(),
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    txChar->setCallbacks(this);

    if (currentDevice.rxUUID.length() > 0) {
        String rxUUID = currentDevice.rxUUID;
        rxUUID.toLowerCase();
        rxChar = pService->createCharacteristic(
            rxUUID.c_str(),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    }

    pService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(svcUUID.c_str());
    pAdvertising->enableScanResponse(true);
    pAdvertising->setName(currentDevice.bleName.c_str());
    pAdvertising->start();

    ESP_LOGI(TAG, "Advertising as %s (free heap: %d)",
             currentDevice.bleName.c_str(), ESP.getFreeHeap());

    if (displayCallback) displayCallback();
    return true;
}

void DeviceEmulator::stopDevice() {
    ESP_LOGI(TAG, "Stopping emulation");
    connected = false;

    if (pServer != nullptr) {
        NimBLEDevice::getAdvertising()->stop();
        NimBLEDevice::getAdvertising()->reset();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    txChar = nullptr;
    rxChar = nullptr;
    pServer = nullptr;
    NimBLEDevice::deinit(true);
    vTaskDelay(200 / portTICK_PERIOD_MS);

    currentDevice = DeviceEntry();
    deviceLoaded = false;
}

void DeviceEmulator::nextDevice() {
    stopDevice();
    int attempts = registry.size();
    int idx = (currentIndex + 1) % registry.size();

    while (attempts-- > 0) {
        if (startDevice(idx)) return;
        idx = (idx + 1) % registry.size();
    }

    ESP_LOGE(TAG, "No valid devices found in registry");
}

void DeviceEmulator::prevDevice() {
    stopDevice();
    int attempts = registry.size();
    int idx = (currentIndex + registry.size() - 1) % registry.size();

    while (attempts-- > 0) {
        if (startDevice(idx)) return;
        idx = (idx + registry.size() - 1) % registry.size();
    }

    ESP_LOGE(TAG, "No valid devices found in registry");
}

void DeviceEmulator::onConnect(NimBLEServer *pServer,
                               NimBLEConnInfo &connInfo) {
    connected = true;
    ESP_LOGI(TAG, "Client connected");
    if (displayCallback) displayCallback();
}

void DeviceEmulator::onDisconnect(NimBLEServer *pServer,
                                  NimBLEConnInfo &connInfo, int reason) {
    connected = false;
    ESP_LOGI(TAG, "Client disconnected (reason: %d)", reason);
    NimBLEDevice::getAdvertising()->start();
    if (displayCallback) displayCallback();
}

void DeviceEmulator::onWrite(NimBLECharacteristic *pCharacteristic,
                             NimBLEConnInfo &connInfo) {
    std::string val = pCharacteristic->getValue();
    String cmd = String(val.c_str());
    ESP_LOGI(TAG, "RX cmd: %s", cmd.c_str());
    handleCommand(cmd);
}

void DeviceEmulator::handleCommand(const String &cmd) {
    if (cmd.startsWith("DeviceType;") || cmd == "DeviceType;") {
        if (rxChar != nullptr && deviceLoaded) {
            String resp =
                currentDevice.identifier.length() > 0
                    ? currentDevice.identifier
                    : "A";
            ESP_LOGI(TAG, "Responding DeviceType: %s", resp.c_str());
            rxChar->setValue(resp.c_str());
            rxChar->notify();
        }
        return;
    }

    if (cmd.startsWith("PowerOff;")) {
        ESP_LOGI(TAG, "PowerOff received (ignored)");
        return;
    }

    if (cmd.startsWith("Battery;")) {
        if (rxChar != nullptr) {
            rxChar->setValue("90");
            rxChar->notify();
        }
        return;
    }

    int colonIdx = cmd.indexOf(':');
    int semicolonIdx = cmd.indexOf(';');
    if (colonIdx < 0) {
        ESP_LOGW(TAG, "Unknown command format: %s", cmd.c_str());
        return;
    }

    String typePart = cmd.substring(0, colonIdx);
    String valuePart = (semicolonIdx > colonIdx)
                           ? cmd.substring(colonIdx + 1, semicolonIdx)
                           : cmd.substring(colonIdx + 1);

    int value = valuePart.toInt();

    String typeNormalized = typePart;
    typeNormalized.toLowerCase();
    while (typeNormalized.length() > 0 &&
           isdigit(typeNormalized[typeNormalized.length() - 1])) {
        typeNormalized.remove(typeNormalized.length() - 1);
    }

    updateFeatureFromCommand(typeNormalized, value);
}

void DeviceEmulator::updateFeatureFromCommand(const String &type, int value) {
    if (!deviceLoaded) return;

    bool found = false;
    for (auto &feat : currentDevice.features) {
        String featType = feat.type;
        featType.toLowerCase();
        if (featType == type) {
            feat.currentValue = constrain(value, feat.minValue, feat.maxValue);
            found = true;
            ESP_LOGI(TAG, "%s = %d", feat.name.c_str(), value);
            break;
        }
    }

    if (!found) {
        ESP_LOGW(TAG, "No feature for type: %s (value: %d)", type.c_str(),
                 value);
    }

    if (displayCallback) displayCallback();
}
