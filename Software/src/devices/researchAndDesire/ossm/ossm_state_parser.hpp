#pragma once

#include <ArduinoJson.h>

#include <cstring>
#include <string>

// Pure helpers for what the OSSM tells us over BLE. No Arduino/NimBLE
// includes so test/test_ossm_state_parser can compile them natively.

// Parsed view of the OSSM state characteristic JSON
// ({"state":"menu.idle",...,"error":"wifi","pairingCode":"AB12","isPaired":true}).
// The optional keys are only present while the OSSM runs a network job.
struct OssmStateInfo {
    std::string state{};
    std::string error{};
    std::string pairingCode{};
    bool isPaired = false;
    std::string targetVersion{};
};

inline bool parseOssmState(const char *json, size_t length, OssmStateInfo &out) {
    JsonDocument doc;
    if (deserializeJson(doc, json, length) != DeserializationError::Ok) return false;
    if (!doc["state"].is<const char *>()) return false;
    out.state = doc["state"].as<const char *>();
    out.error = doc["error"].is<const char *>() ? doc["error"].as<const char *>() : "";
    out.pairingCode = doc["pairingCode"].is<const char *>()
                          ? doc["pairingCode"].as<const char *>()
                          : "";
    out.isPaired = doc["isPaired"].is<bool>() && doc["isPaired"].as<bool>();
    out.targetVersion = doc["targetVersion"].is<const char *>()
                            ? doc["targetVersion"].as<const char *>()
                            : "";
    return true;
}

inline bool ossmStateStartsWith(const std::string &state, const char *prefix) {
    return std::strncmp(state.c_str(), prefix, std::strlen(prefix)) == 0;
}

// On-screen text for a NetworkStatus::error code reported by the OSSM.
inline const char *ossmErrorReason(const std::string &code) {
    if (code == "wifi") return "The OSSM is not connected to Wi-Fi.";
    if (code == "low-memory") return "The OSSM is low on memory. Restart it and try again.";
    if (code == "check-failed") return "The OSSM could not reach the update server.";
    if (code == "install-failed") return "The OSSM could not install the update.";
    if (code == "pairing-failed") return "The OSSM could not reach the dashboard.";
    if (code.empty()) return "The OSSM reported an error.";
    return "The OSSM reported an error.";
}

// Field N of the OSSM pairing characteristic
// "MAC;chipModel;wifiConnected;md5;version[;efuseMacHex]".
inline std::string pairingInfoField(const std::string &data, int fieldIndex) {
    int currentField = 0;
    size_t fieldStart = 0;
    for (size_t i = 0; i <= data.size(); i++) {
        if (i == data.size() || data[i] == ';') {
            if (currentField == fieldIndex) {
                return data.substr(fieldStart, i - fieldStart);
            }
            currentField++;
            fieldStart = i + 1;
        }
    }
    return "";
}

inline bool pairingInfoHasWifi(const std::string &data) {
    return pairingInfoField(data, 2) == "1";
}
