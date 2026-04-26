#ifndef DEVICE_H
#define DEVICE_H

#include <Arduino.h>

#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <components/DisplayObject.h>
#include <functional>
#include <memory>
#include <structs/Menus.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "devices/serviceUUIDs.h"

static const char *TAG = "DEVICE";

// Forward declaration to avoid heavy include and ensure pointer type is known

class Device;

extern Device *device;

struct DeviceCharacteristics {
    NimBLEUUID uuid;
    NimBLERemoteCharacteristic *pCharacteristic = nullptr;

    // Function pointers for custom encode/decode
    std::function<std::string(const std::string &)> encode = nullptr;
    NimBLERemoteCharacteristic::notify_callback notifyCallback = nullptr;
};

struct DeviceDisplayObject {
    std::string name;
    DisplayObject *displayObject;
};

class Device : public NimBLEClientCallbacks {
  public:
    const NimBLEAdvertisedDevice *advertisedDevice;
    NimBLEClient *pClient;
    NimBLERemoteService *pService;

    bool isConnected = false;
    bool isPaused = false;
    bool isTested = false;

    std::vector<MenuItem> menu;
    std::vector<MenuItem> settingsMenu;

    std::unordered_map<std::string, DeviceCharacteristics> characteristics;
    std::vector<std::unique_ptr<DisplayObject>> displayObjects;

    // Constructor with settings document size parameter
    explicit Device(const NimBLEAdvertisedDevice *advertisedDevice);

    // Virtual destructor for proper cleanup
    virtual ~Device();

    // Virtual methods that child classes can optionally override
    virtual void onRightBumperClick() {}
    virtual void onLeftBumperClick() {}
    virtual void onRightButtonClick() {}
    virtual void onLeftButtonClick() {}
    virtual void onExit() {}
    virtual void onPause(bool fullStop = false) {}
    virtual void onResume() {}
    virtual void onConnect() {}
    virtual void onDisconnect() {}
    virtual void onWiFiConnected() {}
    virtual void onDeviceMenuItemSelected(int index) {}
    virtual void onRestart() {}
    virtual void onUpdate() {}
    virtual void onMenuOpen() {}
    virtual void enterStrokeEngineMode() {}
    virtual void enterSimplePenetrationMode() {}
    virtual void enterStreamingMode() {}
    virtual bool isInSimplePenetrationMode() const { return false; }

    virtual void onRightEncoderChange(int value) {}
    virtual void onLeftEncoderChange(int value) {}

    // Override this method to enable persistent left encoder monitoring across
    // device states, such as while in a device_menu
    virtual bool needsPersistentLeftEncoderMonitoring() const { return false; }

    // Override this method to provide the current left encoder value for
    // devices with persistent encoder monitoring
    virtual int getCurrentLeftEncoderValue() const { return 0; }

    // Override this method to provide the left encoder parameter name for
    // devices with persistent encoder monitoring
    virtual const char *getLeftEncoderParameterName() const { return "Value"; }

    virtual void drawControls() {}
    virtual void drawDeviceMenu();
    virtual void drawDeviceSettingsMenu();

    virtual void pullValue() {}
    virtual void pushValue() {}

    virtual NimBLEUUID getServiceUUID() = 0;
    virtual const char *getName() = 0;

    // Display object helpers
    template <typename TDisplayObject, typename... TArgs>
    TDisplayObject *draw(TArgs &&...args) {
        auto uniquePtr = std::make_unique<TDisplayObject>(std::forward<TArgs>(args)...);
        TDisplayObject *rawPtr = uniquePtr.get();
        displayObjects.emplace_back(std::move(uniquePtr));
        return rawPtr;
    }

  protected:
    TaskHandle_t connectionTaskHandle;
    static void connectionTask(void *pvParameter);

    void onConnect(NimBLEClient *pClient) override;

    void onDisconnect(NimBLEClient *pClient, int reason) override;

    bool send(const std::string &command, const std::string &value);

    std::string readString(const std::string &characteristicName);

    int readInt(const std::string &characteristicName, int defaultValue);

    // Helper method to safely read JSON values
    template <typename T>
    T readJsonValue(const std::string &characteristicName, const char *key, T defaultValue) {
        auto value = readString(characteristicName);
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, value.c_str());

        if (error) {
            ESP_LOGE("DEVICE", "JSON parse failed: %s", error.c_str());
            return defaultValue;
        }

        if (!doc.containsKey(key)) {
            ESP_LOGW("DEVICE", "JSON key '%s' not found", key);
            return defaultValue;
        }

        return doc[key].as<T>();
    }

    template <typename T = JsonObject>
    bool readJson(const std::string &command, std::function<void(const T &)> callback) {
        auto value = readString(command);
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, value.c_str());

        if (error) {
            ESP_LOGE("DEVICE", "JSON parse failed: %s", error.c_str());
            return false;
        }

        callback(doc.as<T>());
        return true;
    }

    // Legacy method - now returns a copy of the JSON as string for safety
    std::string readJsonString(const std::string &command);

  private:
    void startConnectionTask();
};

#endif  // DEVICE_H
