#ifndef REGISTRY_H
#define REGISTRY_H

#include <NimBLEUUID.h>
#include <string>
#include <unordered_map>

#include "device.h"

// Factory function type for creating device instances
typedef Device *(*DeviceFactory)(
    const NimBLEAdvertisedDevice *advertisedDevice);

// Global registry map - must call initRegistry() before use
extern std::unordered_map<std::string, DeviceFactory> registry;
extern std::unordered_map<std::string, std::string> registryNames;

// Initialize the device registry (loads from LittleFS)
void initRegistry();

// Look up a device factory by service UUID
const DeviceFactory *getDeviceFactory(const NimBLEUUID &serviceUUID);
const std::string getDeviceName(const NimBLEUUID &serviceUUID,
                                std::string defaultName);

#endif
