#include "ProtocolParser.h"

#include <map>

static const char *TAG = "PROTO_PARSER";

static String sanitizeBleName(const String &pattern) {
    String name = pattern;
    int starIdx = name.indexOf('*');
    if (starIdx >= 0) {
        name = name.substring(0, starIdx) + "Emu";
    }
    if (name.length() > 20) {
        name = name.substring(0, 20);
    }
    return name;
}

static void parseFeatures(const JsonArray &featuresJson,
                          std::vector<EmulatedFeature> &features) {
    features.clear();
    std::map<String, int> typeCounters;

    for (JsonObject feat : featuresJson) {
        JsonObject output = feat["output"];
        if (output.isNull()) continue;

        for (JsonPair kv : output) {
            String type = kv.key().c_str();
            JsonObject featureData = kv.value();

            int minVal = 0;
            int maxVal = 100;
            JsonArray range = featureData["value"];
            if (range.isNull()) range = featureData["position"];
            if (!range.isNull()) {
                minVal = range[0] | 0;
                maxVal = range[1] | 100;
            }

            int idx = typeCounters[type]++;
            String name = type;
            name[0] = toupper(name[0]);
            if (idx > 0) {
                name += " " + String(idx + 1);
            }

            String description = feat["description"] | "";
            if (description.length() > 0) {
                name = description;
            }

            features.push_back({name, type, minVal, maxVal, (float)minVal});
        }
    }
}

bool loadRegistry(std::vector<RegistryEntry> &registry) {
    registry.clear();

    File regFile = LittleFS.open("/registry.json", "r");
    if (!regFile) {
        ESP_LOGE(TAG, "Cannot open /registry.json");
        return false;
    }
    String regStr = regFile.readString();
    regFile.close();
    vTaskDelay(1);

    JsonDocument regDoc;
    if (deserializeJson(regDoc, regStr)) {
        ESP_LOGE(TAG, "Failed to parse registry.json");
        return false;
    }
    vTaskDelay(1);

    for (JsonPair pair : regDoc.as<JsonObject>()) {
        String uuid = pair.key().c_str();
        uuid.toUpperCase();

        JsonArray files = pair.value();
        if (files.isNull()) continue;

        for (JsonString fileRef : files) {
            registry.push_back({uuid, fileRef.c_str()});
        }
        vTaskDelay(1);
    }

    // Sort by config file name for stable ordering
    std::sort(registry.begin(), registry.end(),
              [](const RegistryEntry &a, const RegistryEntry &b) {
                  return a.configFile < b.configFile;
              });

    ESP_LOGI(TAG, "Registry: %d entries", registry.size());
    return !registry.empty();
}

bool loadDeviceFromRegistry(const RegistryEntry &reg, DeviceEntry &entry) {
    File file = LittleFS.open(reg.configFile, "r");
    if (!file) {
        ESP_LOGW(TAG, "Cannot open: %s", reg.configFile.c_str());
        return false;
    }
    String jsonStr = file.readString();
    file.close();
    vTaskDelay(1);

    JsonDocument doc;
    if (deserializeJson(doc, jsonStr)) {
        ESP_LOGW(TAG, "JSON parse failed: %s", reg.configFile.c_str());
        return false;
    }
    vTaskDelay(1);

    entry = DeviceEntry();
    entry.configFile = reg.configFile;
    entry.serviceUUID = reg.serviceUUID;

    JsonObject defaults = doc["defaults"];
    entry.deviceName = defaults["name"] | "Unknown";

    JsonArray defaultFeatures = defaults["features"];
    if (!defaultFeatures.isNull()) {
        parseFeatures(defaultFeatures, entry.features);
    }

    JsonArray configurations = doc["configurations"];
    if (!configurations.isNull() && configurations.size() > 0) {
        JsonObject firstConfig = configurations[0];
        JsonArray identifiers = firstConfig["identifier"];
        if (!identifiers.isNull() && identifiers.size() > 0) {
            entry.identifier = identifiers[0].as<String>();
        }
        if (!firstConfig["name"].isNull()) {
            entry.deviceName = firstConfig["name"].as<String>();
        }
        JsonArray configFeatures = firstConfig["features"];
        if (!configFeatures.isNull()) {
            parseFeatures(configFeatures, entry.features);
        }
    }

    JsonArray commArray = doc["communication"];
    if (commArray.isNull() || commArray.size() == 0) return false;

    JsonObject btle = commArray[0]["btle"];
    if (btle.isNull()) return false;

    JsonArray names = btle["names"];
    if (!names.isNull() && names.size() > 0) {
        entry.bleName = sanitizeBleName(names[0].as<String>());
    } else {
        entry.bleName = "Emulator";
    }

    JsonObject services = btle["services"];
    if (services.isNull()) return false;

    JsonObject chars;
    for (JsonPair kv : services) {
        String key = kv.key().c_str();
        String uuidLower = reg.serviceUUID;
        uuidLower.toLowerCase();
        key.toLowerCase();
        if (key == uuidLower) {
            chars = kv.value();
            break;
        }
    }

    if (chars.isNull()) {
        auto it = services.begin();
        if (it != services.end()) {
            chars = it->value();
        }
    }

    if (chars.isNull()) return false;

    entry.txUUID = chars["tx"] | "";
    entry.rxUUID = chars["rx"] | "";

    if (entry.txUUID.length() == 0) {
        entry.txUUID = chars["command"] | "";
    }

    if (entry.txUUID.length() == 0) {
        ESP_LOGW(TAG, "No tx/command characteristic for %s",
                 reg.configFile.c_str());
        return false;
    }

    ESP_LOGI(TAG, "Loaded: %s (%s) - %d features",
             entry.deviceName.c_str(), entry.bleName.c_str(),
             entry.features.size());
    return true;
}
