#ifndef DEVICE_EMULATOR_H
#define DEVICE_EMULATOR_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <functional>
#include <vector>

#include "ProtocolParser.h"

class DeviceEmulator : public NimBLEServerCallbacks,
                       public NimBLECharacteristicCallbacks {
  public:
    using DisplayCallback = std::function<void()>;

    bool init();
    int getDeviceCount() const;
    bool startDevice(int index);
    void stopDevice();
    void nextDevice();
    void prevDevice();

    const DeviceEntry *getCurrentDevice() const;
    int getCurrentIndex() const;
    bool isConnected() const;

    void setDisplayCallback(DisplayCallback cb);

  private:
    std::vector<RegistryEntry> registry = {};
    DeviceEntry currentDevice = {};
    bool deviceLoaded = false;
    int currentIndex = 0;
    bool connected = false;
    DisplayCallback displayCallback = nullptr;

    NimBLEServer *pServer = nullptr;
    NimBLECharacteristic *txChar = nullptr;
    NimBLECharacteristic *rxChar = nullptr;

    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override;
    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo,
                      int reason) override;
    void onWrite(NimBLECharacteristic *pCharacteristic,
                 NimBLEConnInfo &connInfo) override;

    void handleCommand(const String &cmd);
    void updateFeatureFromCommand(const String &type, int value);
};

#endif
