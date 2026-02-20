#include "device.h"

#include <services/buzzer.h>

#include "pages/genericPages.h"
#include "services/leds.h"
#include "state/remote.h"

Device *device = nullptr;

Device::Device(const NimBLEAdvertisedDevice *advertisedDevice)
    : advertisedDevice(advertisedDevice) {
    startConnectionTask();
}

Device::~Device() {
    displayObjects.clear();
    ESP_LOGD(TAG, "Cleared %zu display objects", displayObjects.size());

    // Clear characteristics map (pointers are managed by NimBLE)
    characteristics.clear();
    ESP_LOGD(TAG, "Cleared characteristics map");

    // Clear menu
    menu.clear();
    ESP_LOGD(TAG, "Cleared menu");

    // clear te client callbacks so that we don't call "this->onDisconnect()"
    if (pClient) {
        pClient->setClientCallbacks(nullptr, true);

        pClient->disconnect();

        pClient = nullptr;
    }

    ESP_LOGD(TAG, "Device destructor completed");
}

void Device::connectionTask(void *pvParameter) {
    Device *device = (Device *)pvParameter;
    const NimBLEAdvertisedDevice *advDevice = device->advertisedDevice;
    bool connected = false;

    updateStatusText("Initializing connection...");

    while (true) {
        ESP_LOGI(TAG, "Connection task running");

        vTaskDelay(1000);
        NimBLEClient *pClient = nullptr;

        /** Check if we have a client we should reuse first **/
        if (NimBLEDevice::getCreatedClientCount()) {
            updateStatusText("Checking existing connections...");
            ESP_LOGD(TAG, "Existing clients detected: %u",
                     NimBLEDevice::getCreatedClientCount());
            if (advDevice) {
                ESP_LOGD(TAG,
                         "Attempting reuse with peer: %s (RSSI during adv: %d)",
                         advDevice->getAddress().toString().c_str(),
                         advDevice->getRSSI());
            }

            /**
             *  Special case when we already know this device, we send false as
             * the second argument in connect() to prevent refreshing the
             * service database. This saves considerable time and power.
             */
            pClient =
                NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
            if (pClient) {
                updateStatusText("Attempting fast reconnect...");
                ESP_LOGD(TAG,
                         "Found existing client for peer: %s; attempting fast "
                         "reconnect (no svc refresh)",
                         advDevice->getAddress().toString().c_str());
                if (!pClient->connect(advDevice, false)) {
                    updateStatusText("Reconnection failed, retrying...");
                    ESP_LOGE(
                        TAG,
                        "Reconnect failed, deleting client and trying again.");
                    NimBLEDevice::deleteClient(pClient);
                    connected = false;
                    continue;
                }
                ESP_LOGI(TAG, "Reconnected client");
            } else {
                /**
                 *  We don't already have a client that knows this device,
                 *  check for a client that is disconnected that we can use.
                 *
                 *  NOTE: Reusing disconnected clients for DIFFERENT devices
                 *  can cause connection issues. Delete stale clients instead
                 *  and create a fresh one for reliable connections.
                 */
                ESP_LOGD(TAG,
                         "No existing client for peer; searching for "
                         "disconnected client slot");
                pClient = NimBLEDevice::getDisconnectedClient();
                if (pClient) {
                    ESP_LOGD(TAG,
                             "Found disconnected client, deleting it to create "
                             "fresh connection");
                    NimBLEDevice::deleteClient(pClient);
                    pClient = nullptr;  // Will create new client below
                } else {
                    ESP_LOGD(TAG,
                             "No disconnected clients available; will create a "
                             "new client");
                }
            }
        }

        vTaskDelay(1);

        /** No client to reuse? Create a new one. */
        if (!pClient) {
            updateStatusText("Creating new connection...");
            ESP_LOGD(TAG, "No client to reuse, creating new client");
            if (NimBLEDevice::getCreatedClientCount() >=
                NIMBLE_MAX_CONNECTIONS) {
                updateStatusText("Connection limit reached!");
                ESP_LOGE(TAG,
                         "Max clients reached - no more connections available");
                connected = false;

                // TODO: Schedule for deleting and throw error.
                break;
            }

            ESP_LOGD(TAG, "Creating new client");
            pClient = NimBLEDevice::createClient();

            ESP_LOGI(TAG, "New client created");

            pClient->setClientCallbacks(device, false);
            /**
             *  Set initial connection parameters:
             *  These settings are safe for 3 clients to connect reliably, can
             * go faster if you have less connections. Timeout should be a
             * multiple of the interval, minimum is 100ms. Min interval: 12
             * * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0 latency, 150 *
             * 10ms = 1500ms timeout
             */
            pClient->setConnectionParams(12, 12, 0, 150);
            ESP_LOGD(TAG,
                     "Connection params set: min_itvl=12, max_itvl=12, "
                     "latency=0, timeout=1500ms");

            /** Set how long we are willing to wait for the connection to
             * complete (milliseconds), default is 30000. */
            pClient->setConnectTimeout(5 * 1000);
            ESP_LOGD(TAG, "Connect timeout set: %dms", 5 * 1000);

            ESP_LOGI(TAG, "Connecting to peer (new client): %s",
                     advDevice->getAddress().toString().c_str());
            updateStatusText(String("Connecting to ") +
                             advDevice->getAddress().toString().c_str() +
                             "...");
            vTaskDelay(11);
            if (!pClient->connect(advDevice, true, false, false)) {
                updateStatusText("Connection failed, please try again.");
                /** Created a client but failed to connect, don't need to keep
                 * it as it has no data */
                NimBLEDevice::deleteClient(pClient);
                ESP_LOGE(TAG, "Failed to connect, deleted client");
                connected = false;
                break;
            }
            ESP_LOGD(TAG, "Initial connect (new client) succeeded");
        }

        vTaskDelay(1);

        ESP_LOGD(TAG, "Checking if client is connected");

        if (!pClient->isConnected()) {
            ESP_LOGW(TAG,
                     "Client instance exists but not connected; attempting "
                     "normal connect");
            if (!pClient->connect(advDevice)) {
                updateStatusText("Connection failed, please try again.");
                ESP_LOGE(TAG, "Failed to connect");
                connected = false;
                break;
            }
            ESP_LOGD(TAG, "Connect after existence check succeeded");
        }
        vTaskDelay(1);

        ESP_LOGI(TAG, "Connected to: %s RSSI: %d",
                 pClient->getPeerAddress().toString().c_str(),
                 pClient->getRssi());
        updateStatusText("Connected! Setting up device...");

        device->pClient = pClient;
        device->pService = pClient->getService(device->getServiceUUID());
        if (device->pService == nullptr) {
            updateStatusText("Device service not found!");
            ESP_LOGE(TAG, "Service not found");
            if (stateMachine) {
                stateMachine->process_event(connected_error_event());
            }
            NimBLEDevice::getScan()->start(0);
            break;
        }

        // for each characteristic in the device characteristics vector, get the
        // characteristic and save it.
        updateStatusText("Discovering device capabilities...");
        for (auto &characteristic : device->characteristics) {
            vTaskDelay(1);
            ESP_LOGD(TAG, "Getting characteristic: %s",
                     characteristic.first.c_str());
            characteristic.second.pCharacteristic =
                device->pService->getCharacteristic(characteristic.second.uuid);

            if (characteristic.second.pCharacteristic->canNotify() &&
                characteristic.second.notifyCallback) {
                // if the user has provided a notify callback, subscribe to the
                // characteristic with the notify callback.
                characteristic.second.pCharacteristic->subscribe(
                    true, characteristic.second.notifyCallback);
            }
        }

        vTaskDelay(1);
        updateStatusText("Initializing device settings...");

        // run the user defined "on connect" method.
        device->onConnect();
        // Now signal the UI/state machine that we're ready
        if (stateMachine) {
            stateMachine->process_event(connected_event());
        }

        ESP_LOGI(TAG, "Done with this device!");
        updateStatusText("Device ready! Loading interface...");
        break;
    }
    vTaskDelete(NULL);
}

void Device::startConnectionTask() {
    // xTaskCreatePinnedToCore(Device::connectionTask, "connectionTask",
    //                         10 * configMINIMAL_STACK_SIZE, this, 1,
    //                         &connectionTaskHandle, 0);
}

void Device::onConnect(NimBLEClient *pClient) {
    ESP_LOGD(TAG, "Connected to %s", getName());
    playBuzzerPattern(BuzzerPattern::DEVICE_CONNECTED);
    setLed(LEDColors::connected, 255, 1500);
}

void Device::onDisconnect(NimBLEClient *pClient, int reason) {
    playBuzzerPattern(BuzzerPattern::DEVICE_DISCONNECTED);
    ESP_LOGD(TAG, "Disconnected from %s", getName());
    NimBLEDevice::getScan()->start(0);
    if (stateMachine) {
        stateMachine->process_event(disconnected_event());
    }
    this->onDisconnect();
    setLed(LEDColors::logoBlue, 255, 1500);
}

bool Device::send(const std::string &command, const std::string &value) {
    auto it = characteristics.find(command);
    if (it == characteristics.end()) {
        ESP_LOGW(TAG, "Characteristic '%s' not found for device '%s'",
                 command.c_str(), getName());
        return false;
    }

    auto *pChr = it->second.pCharacteristic;
    if (!pChr) {
        ESP_LOGW(TAG,
                 "Characteristic '%s' exists but pCharacteristic is nullptr "
                 "for device '%s'",
                 command.c_str(), getName());
        return false;
    }

    if (!pChr->canWrite()) {
        ESP_LOGW(TAG, "Characteristic '%s' for device '%s' is not writable",
                 command.c_str(), getName());
        return false;
    }

    ESP_LOGI(TAG, "Writing value '%s' to characteristic '%s' on device '%s'",
             value.c_str(), command.c_str(), getName());
    std::string encodedValue = value;
    if (it->second.encode) {
        encodedValue = it->second.encode(value);
    }
    return pChr->writeValue(encodedValue);
}

std::string Device::readString(const std::string &characteristicName) {
    auto it = characteristics.find(characteristicName);
    if (it == characteristics.end()) {
        ESP_LOGW(TAG, "Characteristic '%s' not found for device '%s'",
                 characteristicName.c_str(), getName());
        return std::string();
    }

    auto *pChr = it->second.pCharacteristic;
    if (!pChr) {
        ESP_LOGW(TAG,
                 "Characteristic '%s' exists but pCharacteristic is nullptr "
                 "for device '%s'",
                 characteristicName.c_str(), getName());
        return std::string();
    }

    if (!pChr->canRead()) {
        ESP_LOGW(TAG, "Characteristic '%s' for device '%s' is not readable",
                 characteristicName.c_str(), getName());
        return std::string();
    }

    ESP_LOGD(TAG, "Reading value from characteristic '%s' on device '%s'",
             characteristicName.c_str(), getName());
    NimBLEAttValue rawValue = pChr->readValue();

    ESP_LOGI(TAG, "Read value from characteristic '%s' on device '%s': %s",
             characteristicName.c_str(), getName(), rawValue.c_str());
    return rawValue.c_str();
}

int Device::readInt(const std::string &characteristicName, int defaultValue) {
    auto value = readString(characteristicName);
    char *endptr = nullptr;
    long result = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str() || *endptr != '\0') {
        // Parsing failed, fallback to defaultValue
        return defaultValue;
    }
    return static_cast<int>(result);
}

std::string Device::readJsonString(const std::string &command) {
    return readString(command);
}

void Device::drawDeviceMenu() {
    activeMenu = &menu;
    activeMenuCount = menu.size();

    drawMenu();
}
