#ifndef UI_TEXT_PAGES_H
#define UI_TEXT_PAGES_H

#include "DisplayTypes.h"
#include "Strings.h"

namespace ui {

static const TextPage updatePage = {
    .title = strings::UPDATING_TITLE,
    .description = strings::UPDATING_DESCRIPTION,
    .leftButtonText = strings::CANCEL_STRING,
};

static const TextPage updateFilesystemPage = {
    .title = strings::UPDATING_FILESYSTEM_TITLE,
    .description = strings::UPDATING_FILESYSTEM_DESCRIPTION,
    .leftButtonText = strings::CANCEL_STRING,
};

static const TextPage updateSoftwarePage = {
    .title = strings::UPDATING_SOFTWARE_TITLE,
    .description = strings::UPDATING_SOFTWARE_DESCRIPTION,
    .leftButtonText = strings::CANCEL_STRING,
};

static const TextPage updateDonePage = {
    .title = strings::UPDATE_COMPLETE_TITLE,
    .description = strings::UPDATE_COMPLETE_DESCRIPTION,
    .leftButtonText = strings::CANCEL_STRING,
};

static const TextPage deviceSearchPage = {
    .title = strings::DEVICE_SEARCH_TITLE,
    .description = strings::DEVICE_SEARCH_DESCRIPTION,
    .leftButtonText = strings::CANCEL_STRING,
};

static const TextPage deviceConnectingPage = {
    .title = strings::CONNECTING_TITLE,
    .description = strings::CONNECTING_DESCRIPTION,
    .leftButtonText = strings::CANCEL_STRING,
};

static const TextPage deviceStopPage = {
    .title = strings::DEVICE_STOP_TITLE,
    .description = strings::DEVICE_STOP_DESCRIPTION,
    .leftButtonText = strings::GO_BACK,
    .rightButtonText = strings::GO_HOME,
};

static const TextPage wifiSettingsPage = {
    .title = strings::WIFI_SETTINGS_TITLE,
    .description = strings::WIFI_SETTINGS_DESCRIPTION,
    .qrValue = strings::WIFI_SETTINGS_QR_VALUE,
    .leftButtonText = strings::GO_BACK,
};

static const TextPage wifiConnectedPage = {
    .title = strings::WIFI_CONNECTED_TITLE,
    .description = strings::WIFI_CONNECTED_DESCRIPTION,
    .leftButtonText = strings::GO_BACK,
};

}  // namespace ui

#endif  // UI_TEXT_PAGES_H
