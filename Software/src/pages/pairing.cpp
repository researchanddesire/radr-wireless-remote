#include "pairing.h"

#include <ArduinoJson.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <HTTPClient.h>
#include "FirmwareProvenance.h"
#include <components/TextButton.h>
#include <constants/Colors.h>
#include <constants/Sizes.h>
#include <devices/device.h>
#include <pages/displayUtils.h>
#include <pages/genericPages.h>
#include <pins.h>
#include <services/display.h>

// Forward declaration — defined in remote.cpp
extern void fireStateMachineDoneEvent();

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
        "Connect to WiFi first to pair your OSSM with the dashboard.",
    .leftButtonText = GO_BACK,
};

static const char *PAIRING_TAG = "PAIRING";

// Parse a semicolon-delimited string and return the field at the given index.
// Returns empty string if the index is out of range.
static String getField(const std::string &data, int fieldIndex) {
    int currentField = 0;
    size_t fieldStart = 0;

    for (size_t i = 0; i <= data.size(); i++) {
        if (i == data.size() || data[i] == ';') {
            if (currentField == fieldIndex) {
                return String(data.substr(fieldStart, i - fieldStart).c_str());
            }
            currentField++;
            fieldStart = i + 1;
        }
    }

    return "";
}

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

static void ossmPairingTask(void *pvParameters) {
    // Capture device pointer and characteristic early to avoid race
    // with disconnect
    if (device == nullptr || !device->isConnected) {
        ESP_LOGW(PAIRING_TAG, "No device connected");
        updateStatusText("No device connected.");
        vTaskDelete(nullptr);
        return;
    }

    auto it = device->characteristics.find("pairing");
    if (it == device->characteristics.end() ||
        it->second.pCharacteristic == nullptr) {
        ESP_LOGW(PAIRING_TAG, "Pairing characteristic not found");
        updateStatusText("Could not read device info.");
        vTaskDelete(nullptr);
        return;
    }

    // Capture the characteristic pointer before any async disconnect
    NimBLERemoteCharacteristic *pairingChar = it->second.pCharacteristic;

    // Read OSSM device info from BLE
    // Format: "MAC;chipModel;wifiConnected;md5;version;efuseMacHex"
    std::string pairingInfo;
    try {
        pairingInfo = pairingChar->readValue();
    } catch (...) {
        ESP_LOGW(PAIRING_TAG, "BLE read failed");
        updateStatusText("Could not read device info.");
        vTaskDelete(nullptr);
        return;
    }

    if (pairingInfo.empty()) {
        ESP_LOGW(PAIRING_TAG, "Empty pairing info");
        updateStatusText("Could not read device info.");
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(PAIRING_TAG, "Pairing info: %s", pairingInfo.c_str());

    // Parse fields
    String mac = getField(pairingInfo, 0);
    String md5 = getField(pairingInfo, 3);
    String version = getField(pairingInfo, 4);
    String efuseMacHex = getField(pairingInfo, 5);

    if (mac.isEmpty()) {
        ESP_LOGW(PAIRING_TAG, "Could not parse MAC from pairing info");
        updateStatusText("Could not parse device info.");
        vTaskDelete(nullptr);
        return;
    }

    // Build JSON payload matching the OSSM's own /api/ossm/auth call
    JsonDocument doc;
    doc["mac"] = mac;
    doc["chip"] = efuseMacHex;
    doc["md5"] = md5;
    doc["device"] = "OSSM";
    doc["version"] = version;

    String body;
    serializeJson(doc, body);

    // POST to RAD Dashboard
    String url = String(RAD_SERVER) + "/api/ossm/auth";
    ESP_LOGI(PAIRING_TAG, "POST %s", url.c_str());

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-RAD-Firmware-Provenance-Capability", "1");
    const auto provenanceToken = firmware::provenance::currentToken();
    if (!provenanceToken.empty()) {
        http.addHeader("X-RAD-Firmware-Provenance", provenanceToken.c_str());
        http.addHeader("X-RAD-Firmware-Provenance-ID",
                       firmware::provenance::currentTokenId().c_str());
        http.addHeader("X-RAD-Firmware-Image-SHA256",
                       firmware::provenance::currentImageSha256().c_str());
    }

    int httpCode = http.POST(body);

    if (httpCode != 200) {
        ESP_LOGW(PAIRING_TAG, "Auth failed with HTTP %d", httpCode);
        String errorMsg = "Pairing failed (HTTP " + String(httpCode) + ")";
        http.end();
        updateStatusText(errorMsg);
        vTaskDelete(nullptr);
        return;
    }

    String payload = http.getString();
    http.end();

    JsonDocument resp;
    DeserializationError err = deserializeJson(resp, payload);
    if (err) {
        ESP_LOGW(PAIRING_TAG, "JSON parse error: %s", err.c_str());
        updateStatusText("Invalid server response.");
        vTaskDelete(nullptr);
        return;
    }

    String pairingCode = resp["pairingCode"].as<String>();
    bool isPaired = resp["isPaired"].as<bool>();
    ESP_LOGI(PAIRING_TAG, "Auth response: code=%s isPaired=%d",
             pairingCode.c_str(), isPaired);

    if (isPaired) {
        // Already paired — transition to success screen
        fireStateMachineDoneEvent();
        vTaskDelete(nullptr);
        return;
    }

    // Not yet paired — draw the pairing code and QR code
    drawPairingCodeScreen(pairingCode);

    vTaskDelete(nullptr);
}

void startOssmPairingCheck() {
    xTaskCreatePinnedToCore(ossmPairingTask, "ossmPairingTask",
                            20 * configMINIMAL_STACK_SIZE, nullptr, 1, nullptr,
                            1);
}
