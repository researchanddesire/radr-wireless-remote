#ifndef TEXT_PAGES_H
#define TEXT_PAGES_H

#include "Arduino.h"
#include <Strings.h>

struct TextPage {
    String title;
    String description;
    String qrValue = ui::strings::EMPTY_STRING;

    String leftButtonText = ui::strings::EMPTY_STRING;
    String rightButtonText = ui::strings::EMPTY_STRING;
};

static const TextPage updatePage = {
    .title = ui::strings::UPDATING_TITLE,
    .description = ui::strings::UPDATING_DESCRIPTION,
    .leftButtonText = ui::strings::CANCEL_STRING,
};

static const TextPage updateFilesystemPage = {
    .title = ui::strings::UPDATING_FILESYSTEM_TITLE,
    .description = ui::strings::UPDATING_FILESYSTEM_DESCRIPTION,
    .leftButtonText = ui::strings::CANCEL_STRING,
};

static const TextPage updateSoftwarePage = {
    .title = ui::strings::UPDATING_SOFTWARE_TITLE,
    .description = ui::strings::UPDATING_SOFTWARE_DESCRIPTION,
    .leftButtonText = ui::strings::CANCEL_STRING,
};

static const TextPage updateDonePage = {
    .title = ui::strings::UPDATE_COMPLETE_TITLE,
    .description = ui::strings::UPDATE_COMPLETE_DESCRIPTION,
    .leftButtonText = ui::strings::CANCEL_STRING,
};

static const TextPage deviceSearchPage = {
    .title = ui::strings::DEVICE_SEARCH_TITLE,
    .description = ui::strings::DEVICE_SEARCH_DESCRIPTION,
    .leftButtonText = ui::strings::CANCEL_STRING,
};

static const TextPage deviceConnectingPage = {
    .title = ui::strings::CONNECTING_TITLE,
    .description = ui::strings::CONNECTING_DESCRIPTION,
    .leftButtonText = ui::strings::CANCEL_STRING,
};

static const TextPage deviceStopPage = {
    .title = ui::strings::DEVICE_STOP_TITLE,
    .description = ui::strings::DEVICE_STOP_DESCRIPTION,
    .leftButtonText = ui::strings::GO_BACK,
    .rightButtonText = ui::strings::GO_HOME,
};

static const TextPage wifiSettingsPage = {
    .title = ui::strings::WIFI_SETTINGS_TITLE,
    .description = ui::strings::WIFI_SETTINGS_DESCRIPTION,
    .qrValue = ui::strings::WIFI_SETTINGS_QR_VALUE,
    .leftButtonText = ui::strings::GO_BACK,
};

static const TextPage wifiConnectedPage = {
    .title = ui::strings::WIFI_CONNECTED_TITLE,
    .description = ui::strings::WIFI_CONNECTED_DESCRIPTION,
    .leftButtonText = ui::strings::GO_BACK,
};

#endif  // TEXT_PAGES_H