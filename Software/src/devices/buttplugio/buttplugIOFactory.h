#ifndef BUTTPLUGIO_DEVICE_H
#define BUTTPLUGIO_DEVICE_H

#include <Arduino.h>

#include <LittleFS.h>
#include <regex>

#include "../device.h"
#include "../lovense/LovenseGeneric.hpp"
#include "GenericButtplugIODevice.hpp"

/// @brief A device factory for devices that use the ButtplugIO protocol.
/// @param advertisedDevice The advertised BLE device to create a device for
/// @return A pointer to the created Device, or nullptr if creation failed
Device* ButtplugIODeviceFactory(const NimBLEAdvertisedDevice* advertisedDevice);

#endif  // BUTTPLUGIO_DEVICE_H
