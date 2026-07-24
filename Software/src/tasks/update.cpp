#include "update.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

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

std::uint32_t physicalFlashSize() {
    std::uint32_t size = 0;
    return esp_flash_get_physical_size(nullptr, &size) == ESP_OK
               ? size
               : ESP.getFlashChipSize();
}

std::string runningFirmwareHash() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    unsigned char digest[32] = {};
    if (running == nullptr ||
        esp_partition_get_sha256(running, digest) != ESP_OK) {
        return {};
    }
    return firmware::sha256Hex(digest);
}

firmware::DeviceReport makeDeviceReport() {
    firmware::DeviceReport report;
    report.deviceType = "radr";
    report.deviceId = stableDeviceId();
    report.reportedTrack = FIRMWARE_TRACK;
    report.currentVersion = VERSION;
    report.currentBuild = FIRMWARE_BUILD_SHA;
    report.firmwareHash = runningFirmwareHash();
    report.chip = ESP.getChipModel();
    report.chipRevision = ESP.getChipRevision();
    report.chipCores = ESP.getChipCores();
    report.hardwareRevision = "radr-v1";
    report.flashSizeBytes = physicalFlashSize();
    const esp_partition_t *nextUpdatePartition =
        esp_ota_get_next_update_partition(nullptr);
    report.otaSlotSizeBytes =
        nextUpdatePartition == nullptr ? 0 : nextUpdatePartition->size;
    report.psramSizeBytes = ESP.getPsramSize();
    report.partitionLayout = "radr-ota-v1";
    return report;
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
        } else if (firmware::isMetadataArtifactRole(artifact.role)) {
            continue;
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

bool confirmRunningFirmware() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == nullptr) return false;

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t stateResult = esp_ota_get_state_partition(running, &state);
    if (stateResult == ESP_ERR_NOT_SUPPORTED ||
        (stateResult == ESP_OK && state != ESP_OTA_IMG_PENDING_VERIFY)) {
        return true;
    }
    if (stateResult != ESP_OK) {
        ESP_LOGW(UPDATE_TAG, "Unable to read running OTA state: %s",
                 esp_err_to_name(stateResult));
        return false;
    }

    const esp_err_t confirmResult = esp_ota_mark_app_valid_cancel_rollback();
    if (confirmResult != ESP_OK) {
        ESP_LOGE(UPDATE_TAG, "Unable to confirm running firmware: %s",
                 esp_err_to_name(confirmResult));
        return false;
    }
    ESP_LOGI(UPDATE_TAG, "Confirmed running OTA image");
    return true;
}

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
             decision.shouldUpdate ? "true" : "false",
             decision.targetVersion.c_str(), decision.nextHopVersion.c_str());
    if (!decision.shouldUpdate) return false;
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
