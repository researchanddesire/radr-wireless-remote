#ifndef REGISTRY_H
#define REGISTRY_H

#include <NimBLEUUID.h>

#include "device.h"

// Factory function type for creating device instances
typedef Device *(*DeviceFactory)(
    const NimBLEAdvertisedDevice *advertisedDevice);

// Initialize the device registry (loads registry.json into memory)
void initRegistry();

// Look up a device factory by service UUID (lazy lookup from in-memory JSON)
const DeviceFactory *getDeviceFactory(const NimBLEUUID &serviceUUID);

#endif
