#include "update.h"

#include <LittleFS.h>
#include <WiFi.h>

#include "FirmwareUpdateRuntime.h"
#include "constants/Version.h"
#include "state/remote.h"

#ifndef FIRMWARE_BUILD_SHA
#define FIRMWARE_BUILD_SHA "unknown"
#endif

#ifndef FIRMWARE_TRACK
#define FIRMWARE_TRACK "main"
#endif

namespace {

constexpr const char *UPDATE_TAG = "UPDATE";
firmware::Decision pendingDecision;
bool decisionReady = false;

std::string stableDeviceId() {
    char identifier[17] = {};
    snprintf(identifier, sizeof(identifier), "%016llx",
             static_cast<unsigned long long>(ESP.getEfuseMac()));
    return identifier;
}

firmware::DeviceReport makeDeviceReport() {
    return {
        .deviceType = "radr",
        .deviceId = stableDeviceId(),
        .reportedTrack = FIRMWARE_TRACK,
        .currentVersion = VERSION,
        .currentBuild = FIRMWARE_BUILD_SHA,
        .firmwareHash = "",
        .chip = std::string(ESP.getChipModel()),
        .hardwareRevision = "radr-v1",
        .flashSizeBytes = ESP.getFlashChipSize(),
        .partitionLayout = "radr-ota-v1",
    };
}

bool validateInstallPlan(const firmware::Decision &decision, String &error) {
    bool sawFilesystem = false;
    bool sawApplication = false;
    for (std::size_t index = 0; index < decision.artifactCount; ++index) {
        const auto &artifact = decision.artifacts[index];
        if (artifact.role == "filesystem") {
            if (sawFilesystem || sawApplication) {
                error = "filesystem must be installed once and before application";
                return false;
            }
            sawFilesystem = true;
        } else if (artifact.role == "application") {
            if (sawApplication) {
                error = "application artifact appears more than once";
                return false;
            }
            sawApplication = true;
        } else {
            error = ("unsupported RADR artifact role: " + artifact.role).c_str();
            return false;
        }
    }
    if (!sawApplication) {
        error = "release has no application artifact";
        return false;
    }
    return true;
}

const firmware::Artifact *artifactForRole(const char *role) {
    if (!decisionReady) return nullptr;
    for (std::size_t index = 0; index < pendingDecision.artifactCount; ++index) {
        if (pendingDecision.artifacts[index].role == role) {
            return &pendingDecision.artifacts[index];
        }
    }
    return nullptr;
}

void finishTask() {
    stateMachine->process_event(done{});
    vTaskDelete(nullptr);
}

}  // namespace

TaskHandle_t updateTaskHandle = nullptr;
TaskHandle_t updateFilesystemTaskHandle = nullptr;
TaskHandle_t updateSoftwareTaskHandle = nullptr;

bool isSoftwareUpdateAvailable = false;
bool isFilesystemUpdateAvailable = false;

bool isUpdateAvailable() {
    if (WiFi.status() != WL_CONNECTED) return false;

    firmware::Decision decision;
    String error;
    const auto report = makeDeviceReport();
    if (!firmware::postCheck(RAD_SERVER, report, decision, error)) {
        ESP_LOGE(UPDATE_TAG, "Firmware resolver failed: %s", error.c_str());
        return false;
    }
    ESP_LOGI(UPDATE_TAG,
             "Resolver assigned track=%s update=%s target=%s next=%s",
             decision.assignedTrack.c_str(),
             decision.updateAvailable ? "true" : "false",
             decision.targetVersion.c_str(), decision.nextHopVersion.c_str());
    if (!decision.updateAvailable) return false;
    if (!validateInstallPlan(decision, error)) {
        ESP_LOGE(UPDATE_TAG, "Invalid install plan: %s", error.c_str());
        return false;
    }

    pendingDecision = decision;
    decisionReady = true;
    isFilesystemUpdateAvailable = artifactForRole("filesystem") != nullptr;
    isSoftwareUpdateAvailable = artifactForRole("application") != nullptr;
    return isSoftwareUpdateAvailable;
}

void updateTask(void *pvParameters) {
    isFilesystemUpdateAvailable = false;
    isSoftwareUpdateAvailable = false;
    decisionReady = false;
    isUpdateAvailable();
    finishTask();
}

void updateFilesystemTask(void *pvParameters) {
    const firmware::Artifact *artifact = artifactForRole("filesystem");
    if (WiFi.status() != WL_CONNECTED || artifact == nullptr) {
        ESP_LOGE(UPDATE_TAG, "Filesystem update is no longer available");
        isFilesystemUpdateAvailable = false;
        finishTask();
        return;
    }

    LittleFS.end();
    String error;
    const bool installed =
        firmware::installStreamedArtifact(*artifact, U_SPIFFS, error);
    if (!LittleFS.begin()) {
        ESP_LOGE(UPDATE_TAG, "Failed to remount LittleFS after update");
    }
    if (!installed) {
        ESP_LOGE(UPDATE_TAG, "Filesystem update failed: %s", error.c_str());
        // Do not install the application when the ordered filesystem step did
        // not verify. The existing bootable application remains selected.
        isSoftwareUpdateAvailable = false;
    }
    isFilesystemUpdateAvailable = false;
    finishTask();
}

void updateSoftwareTask(void *pvParameters) {
    const firmware::Artifact *artifact = artifactForRole("application");
    if (WiFi.status() != WL_CONNECTED || artifact == nullptr) {
        ESP_LOGE(UPDATE_TAG, "Application update is no longer available");
        isSoftwareUpdateAvailable = false;
        finishTask();
        return;
    }

    String error;
    if (!firmware::installStreamedArtifact(*artifact, U_FLASH, error)) {
        ESP_LOGE(UPDATE_TAG, "Application update failed: %s", error.c_str());
        isSoftwareUpdateAvailable = false;
        finishTask();
        return;
    }

    ESP_LOGI(UPDATE_TAG, "Verified application installed; restarting");
    esp_restart();
}
