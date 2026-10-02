#include "pairing.h"

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <components/TextButton.h>
#include <constants/Colors.h>
#include <constants/Sizes.h>
#include <devices/device.h>
#include <esp_heap_caps.h>
#include <pages/displayUtils.h>
#include <pages/genericPages.h>
#include <pins.h>
#include <services/display.h>

#include "devices/researchAndDesire/ossm/ossm_state.h"
// Defined in remote.cpp; keeps the state-machine headers (and their
// header-static pages) out of this translation unit.
void fireStateMachineOssmNoWifiEvent();
void fireStateMachineOssmUnsupportedEvent();
void fireStateMachineOssmLinkLostEvent();
void fireStateMachineTaskFailedEvent();
#include "utils/psramTask.h"

// OSSM Pairing page definitions (extern-declared in TextPages.h)
const TextPage ossmPairingConnectingPage = {
    .title = "OSSM Pairing",
    .description = "Connecting to the RAD Dashboard...",
    .leftButtonText = GO_BACK,
};

const TextPage ossmPairingSuccessPage = {
    .title = "Already Paired",
    .description =
        "This OSSM is already linked to your RAD Dashboard account.",
    .leftButtonText = GO_BACK,
    .rightButtonText = GO_HOME,
};

const TextPage ossmPairingWifiPage = {
    .title = "WiFi Required",
    .description =
        "Your OSSM needs WiFi to pair with the dashboard. Share this "
        "remote's WiFi with it, then try again.",
    .leftButtonText = GO_BACK,
    .rightButtonText = "Share Wi-Fi",
};

const TextPage ossmPairingFailedPage = {
    .title = "Pairing Failed",
    .description = "",
    .leftButtonText = GO_BACK,
};

const TextPage ossmUnsupportedPage = {
    .title = "Update Your OSSM",
    .description =
        "Your OSSM firmware is too old for this feature. Update the OSSM "
        "using the web flasher or contact support.",
    .leftButtonText = GO_BACK,
};

static const char *PAIRING_TAG = "PAIRING";

// How long the OSSM gets to enter its pairing flow after go:pairing before
// we assume its firmware does not know the command.
static constexpr uint32_t OSSM_RESPONSE_TIMEOUT_MS = 10000;
static constexpr uint32_t OSSM_POLL_INTERVAL_MS = 400;

static void drawPairingCodeScreen(const String &pairingCode) {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return;
    }

    // Clear page area inline (clearPage() can't be used here because it
    // tries to re-acquire displayMutex which we already hold, and it's
    // non-recursive — so clearPage() silently skips the clear).
    tft.fillRect(0, Display::PageY, Display::WIDTH, Display::PageHeight,
                 Colors::black);
    int cornerWidth =
        (Display::WIDTH / 2) - (Display::StatusbarWidth / 2);
    tft.fillRect(0, 0, cornerWidth, Display::StatusbarHeight, Colors::black);
    tft.fillRect(Display::WIDTH - cornerWidth, 0, cornerWidth,
                 Display::StatusbarHeight, Colors::black);

    // Title
    tft.setFont(&FreeSansBold12pt7b);
    tft.setTextColor(Colors::white);

    int16_t x1, y1;
    uint16_t tw, th;
    tft.getTextBounds("OSSM Pairing", 0, 0, &x1, &y1, &tw, &th);
    int16_t titleX = (Display::WIDTH - tw) / 2;
    int16_t titleY = Display::PageY + Display::Padding::P3 - y1;
    tft.setCursor(titleX, titleY);
    tft.print("OSSM Pairing");

    // Description text (left side, next to QR code)
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(Colors::lightGray);

    const int16_t textMargin = Display::Padding::P2;
    int16_t descY = titleY + th + Display::Padding::P3;

    // QR code (right-aligned, next to text)
    String qrUrl = String(RAD_SERVER) + "?ossm=" + pairingCode;
    int qrCodeWidth = drawQRCode(
        tft, qrUrl,
        {.y = descY,
         .maxHeight = Display::PageHeight - descY - textMargin});

    wrapText(tft, "Enter on your\nRAD Dashboard:",
             {.x = textMargin,
              .y = descY,
              .rightPadding = textMargin + qrCodeWidth});

    // Pairing code — large and prominent
    tft.setFont(&FreeSansBold12pt7b);
    tft.setTextColor(Colors::white);

    tft.getTextBounds(pairingCode.c_str(), 0, 0, &x1, &y1, &tw, &th);
    int16_t codeX = textMargin;
    int16_t codeY = descY + Display::Padding::P4 + Display::Padding::P3 - y1;
    tft.setCursor(codeX, codeY);
    tft.print(pairingCode);

    xSemaphoreGive(displayMutex);

    // Back button (outside mutex — TextButton manages its own drawing)
    const int16_t buttonY = Display::HEIGHT - 30;
    TextButton backButton("Back", pins::BTN_UNDER_L, 20, buttonY);
    backButton.tick();
}

void drawOssmPairingCodeFromState() {
    const OssmObservedState observed = getOssmObservedState();
    drawPairingCodeScreen(String(observed.info.pairingCode.c_str()));
}

void drawOssmFailurePage(const TextPage &page, const char *reason) {
    const OssmObservedState observed = getOssmObservedState();
    clearPage();
    createPsramTask(drawPageTask, "drawPageTask", 5 * configMINIMAL_STACK_SIZE,
                    const_cast<TextPage *>(&page), 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(150));  // let the page paint before the reason
    updateStatusText(reason != nullptr ? reason
                                       : ossmErrorReason(observed.info.error));
}

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
static const char *const TERMINAL_STATES[] = {"pairing.failed", "pairing.success", "pairing.success.idle"};

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

static void ossmPairingTask(void *) {
    ESP_LOGW(PAIRING_TAG, "[MEM] request: internal free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT));
    const std::string pairingInfo = readOssmPairingInfo();
    if (pairingInfo.empty()) {
        ESP_LOGW(PAIRING_TAG, "Could not read OSSM pairing info");
        // A failed read on a "connected" OSSM means the link is dead (the
        // OSSM rebooted or crashed mid-connect and the disconnect callback
        // never fired). Treat it as a disconnect so the RADR cleans up.
        updateStatusText("Lost the OSSM connection.");
        fireStateMachineOssmLinkLostEvent();
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(PAIRING_TAG, "OSSM pairing info: %s", pairingInfo.c_str());

    if (!pairingInfoHasWifi(pairingInfo)) {
        fireStateMachineOssmNoWifiEvent();
        vTaskDelete(nullptr);
        return;
    }

    if (device != nullptr) device->onPairing();
    followOssmFlow("pairing");
    vTaskDelete(nullptr);
}

void startOssmPairingRequest() {
    if (createInternalTask(ossmPairingTask, "ossmPairingTask",
                           8 * configMINIMAL_STACK_SIZE, nullptr, 1, nullptr,
                           1) != pdPASS) {
        // task_failed_event -> ossm_pairing_failed with the out-of-memory reason
        fireStateMachineTaskFailedEvent();
    }
}
