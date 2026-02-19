#ifndef PROTOCOL_PARSER_H
#define PROTOCOL_PARSER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>

struct EmulatedFeature {
    String name = "";
    String type = "";
    int minValue = 0;
    int maxValue = 100;
    float currentValue = 0;
};

struct DeviceEntry {
    String deviceName = "";
    String serviceUUID = "";
    String txUUID = "";
    String rxUUID = "";
    String bleName = "";
    String identifier = "";
    String configFile = "";
    std::vector<EmulatedFeature> features = {};
};

struct RegistryEntry {
    String serviceUUID = "";
    String configFile = "";
};

bool loadRegistry(std::vector<RegistryEntry> &registry);

bool loadDeviceFromRegistry(const RegistryEntry &reg, DeviceEntry &entry);

#endif
