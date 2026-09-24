#ifndef TEXT_PAGES_H
#define TEXT_PAGES_H

#include "Arduino.h"
#include "constants/Strings.h"

struct TextPage {
    String title;
    String description;
    String qrValue = EMPTY_STRING;

    String leftButtonText = EMPTY_STRING;
    String rightButtonText = EMPTY_STRING;
};

static const TextPage updatePage = {
    .title = UPDATING_TITLE,
    .description = UPDATING_DESCRIPTION,
    .leftButtonText = CANCEL_STRING,
};

static const TextPage updateFilesystemPage = {
    .title = "Updating Filesystem",
    .description = "Updating filesystem...",
    .leftButtonText = CANCEL_STRING,
};

static const TextPage updateSoftwarePage = {
    .title = "Updating Software",
    .description = "Updating software...",
    .leftButtonText = CANCEL_STRING,
};


static const TextPage deviceSearchPage = {
    .title = DEVICE_SEARCH_TITLE,
    .description = DEVICE_SEARCH_DESCRIPTION,
    .leftButtonText = CANCEL_STRING,
};

static const TextPage deviceConnectingPage = {
    .title = "Connecting",
    .description = "Connecting to device...",
    .leftButtonText = CANCEL_STRING,
};

static const TextPage deviceStopPage = {.title = DEVICE_STOP_TITLE,
                                        .description = DEVICE_STOP_DESCRIPTION,
                                        .leftButtonText = GO_BACK,
                                        .rightButtonText = GO_HOME};

static const TextPage wifiSettingsPage = {
    .title = WIFI_SETTINGS_TITLE,
    .description = WIFI_SETTINGS_DESCRIPTION,
    .qrValue = WIFI_SETTINGS_QR_VALUE,
    .leftButtonText = GO_BACK};

static const TextPage wifiConnectedPage = {
    .title = WIFI_CONNECTED_TITLE,
    .description = WIFI_CONNECTED_DESCRIPTION,
    .leftButtonText = GO_BACK};

// Self-update result pages (defined in genericPages.cpp)
extern const TextPage updateUpToDatePage;
extern const TextPage updateFailedPage;

// OSSM Pages (defined in genericPages.cpp — extern to avoid 11x static duplication)
extern const TextPage ossmHelpPage;
extern const TextPage ossmRestartConfirmPage;
extern const TextPage ossmRestartingPage;
extern const TextPage streamingPage;

// OSSM Pairing Pages (defined in pairing.cpp)
extern const TextPage ossmPairingConnectingPage;
extern const TextPage ossmPairingSuccessPage;
extern const TextPage ossmPairingWifiPage;
extern const TextPage ossmPairingFailedPage;

// Shown when the OSSM firmware predates BLE-triggered pairing/update
// (defined in pairing.cpp)
extern const TextPage ossmUnsupportedPage;

// OSSM Update Pages (defined in ossmUpdate.cpp)
extern const TextPage ossmUpdateCheckPage;
extern const TextPage ossmUpdateUpdatingPage;
extern const TextPage ossmUpdateNonePage;
extern const TextPage ossmUpdateWifiPage;
extern const TextPage ossmUpdateFailedPage;
extern const TextPage ossmUpdateAvailablePage;

#endif  // TEXT_PAGES_H