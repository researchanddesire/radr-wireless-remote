#include "ossmUpdate.h"

#include <devices/device.h>
#include <esp_heap_caps.h>
#include <pages/displayUtils.h>
#include <pages/genericPages.h>

#include "constants/Strings.h"
#include "devices/researchAndDesire/ossm/ossm_state.h"
#include "pages/TextPages.h"
// Defined in remote.cpp; keeps the state-machine headers (and their
// header-static pages) out of this translation unit.
void fireStateMachineOssmNoWifiEvent();
void fireStateMachineOssmUnsupportedEvent();
void fireStateMachineOssmLinkLostEvent();
void fireStateMachineTaskFailedEvent();
#include "utils/psramTask.h"

// OSSM Update page definitions (extern-declared in TextPages.h)
const TextPage ossmUpdateCheckPage = {
    .title = "Update OSSM",
    .description = "Checking for updates...",
    .leftButtonText = GO_BACK,
};

const TextPage ossmUpdateUpdatingPage = {
    .title = "Updating OSSM",
    .description =
        "Your OSSM is downloading and installing an update. "
        "Please wait...",
};

const TextPage ossmUpdateNonePage = {
    .title = "Up to Date",
    .description = "Your OSSM is running the latest firmware.",
    .leftButtonText = GO_BACK,
};

const TextPage ossmUpdateWifiPage = {
    .title = "WiFi Required",
    .description =
        "Your OSSM needs WiFi to check for updates. Share this remote's "
        "WiFi with it, then try again.",
    .leftButtonText = GO_BACK,
    .rightButtonText = "Share Wi-Fi",
};

const TextPage ossmUpdateFailedPage = {
    .title = "Update Failed",
    .description = "",
    .leftButtonText = GO_BACK,
};

const TextPage ossmUpdateAvailablePage = {
    .title = "Update Available",
    .description = "",
    .leftButtonText = CANCEL_STRING,
    .rightButtonText = "Update",
};

static const char *UPDATE_TAG = "OSSM_UPDATE";
static constexpr uint32_t OSSM_RESPONSE_TIMEOUT_MS = 10000;
static constexpr uint32_t OSSM_POLL_INTERVAL_MS = 400;

// Reads the OSSM pairing characteristic; empty on any failure.
static std::string readOssmPairingInfo() {
    if (device == nullptr || !device->isConnected) return "";
    auto it = device->characteristics.find("pairing");
    if (it == device->characteristics.end() ||
        it->second.pCharacteristic == nullptr) {
        return "";
    }
    try {
        return it->second.pCharacteristic->readValue();
    } catch (...) {
        return "";
    }
}

// Follows the OSSM's state characteristic by polling while this page's
// follow generation is current and the OSSM stays connected. The first
// OSSM_RESPONSE_TIMEOUT_MS decide whether the OSSM entered the requested flow
// (state prefix); if it never does, its firmware predates the command.
static const char *const TERMINAL_STATES[] = {"update.failed", "update.idle"};

static void followOssmFlow(const char *prefix) {
    const uint32_t generation = ossmFollowGeneration();
    const uint32_t responseDeadline = millis() + OSSM_RESPONSE_TIMEOUT_MS;
    bool entered = false;
    while (ossmFollowGeneration() == generation && device != nullptr &&
           device->isConnected) {
        const std::string json = device->readRawState();
        if (!json.empty()) ingestOssmStateJson(json.c_str(), json.size());
        {
            const OssmObservedState observed = getOssmObservedState();
            if (observed.valid) {
                for (const char *terminal : TERMINAL_STATES) {
                    if (observed.info.state == terminal) return;
                }
            }
        }
        if (!entered) {
            const OssmObservedState observed = getOssmObservedState();
            if (observed.valid && ossmStateStartsWith(observed.info.state, prefix)) {
                entered = true;
            } else if ((int32_t)(responseDeadline - millis()) <= 0) {
                fireStateMachineOssmUnsupportedEvent();
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(OSSM_POLL_INTERVAL_MS));
    }
}

static void ossmUpdateTask(void *) {
    ESP_LOGW(UPDATE_TAG, "[MEM] request: internal free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT));
    const std::string pairingInfo = readOssmPairingInfo();
    if (pairingInfo.empty()) {
        ESP_LOGW(UPDATE_TAG, "Could not read OSSM pairing info");
        // A failed read on a "connected" OSSM means the link is dead (the
        // OSSM rebooted or crashed mid-connect and the disconnect callback
        // never fired). Treat it as a disconnect so the RADR cleans up.
        updateStatusText("Lost the OSSM connection.");
        fireStateMachineOssmLinkLostEvent();
        vTaskDelete(nullptr);
        return;
    }

    if (!pairingInfoHasWifi(pairingInfo)) {
        fireStateMachineOssmNoWifiEvent();
        vTaskDelete(nullptr);
        return;
    }

    if (device != nullptr) device->onUpdate();
    followOssmFlow("update");
    vTaskDelete(nullptr);
}

void startOssmUpdateRequest() {
    if (createInternalTask(ossmUpdateTask, "ossmUpdateTask",
                           8 * configMINIMAL_STACK_SIZE, nullptr, 1, nullptr,
                           1) != pdPASS) {
        // task_failed_event -> ossm_update_failed with the out-of-memory reason
        fireStateMachineTaskFailedEvent();
    }
}

void drawOssmUpdateAvailableFromState() {
    const OssmObservedState observed = getOssmObservedState();
    clearPage();
    createPsramTask(drawPageTask, "drawPageTask", 5 * configMINIMAL_STACK_SIZE,
                    const_cast<TextPage *>(&ossmUpdateAvailablePage), 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    String text = "A firmware update";
    if (!observed.info.targetVersion.empty()) {
        text += " (";
        text += observed.info.targetVersion.c_str();
        text += ")";
    }
    text += " is available for your OSSM. This will restart the device.";
    updateStatusText(text);
}

void confirmOssmInstall() {
    if (device != nullptr) device->onUpdate();  // OSSM treats it as the confirmation
}
