
#ifndef LOCKBOX_COMS_H
#define LOCKBOX_COMS_H

#include <ArduinoJson.h>
#include <NimBLEClient.h>
#include <NimBLEDevice.h>
#include <devices/device.h>
#include <devices/registry.h>
#include <structs/SettingPercents.h>
#include <vector>

#include "state/remote.h"

struct DiscoveredDevice {
    const NimBLEAdvertisedDevice *advertisedDevice;
    const DeviceFactory *factory;
    std::string name;
    int rssi;
};

void sendCommand(const String &command);

void initBLE();

// Device list management
std::vector<DiscoveredDevice> &getDiscoveredDevices();
void clearDiscoveredDevices();
void connectToDiscoveredDevice(int index);
bool startScanWithTimeout(int timeoutMs, void (*onComplete)());

#endif
