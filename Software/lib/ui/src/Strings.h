#ifndef UI_STRINGS_H
#define UI_STRINGS_H

#include "Progmem.h"

namespace ui {
namespace strings {

// ============================================================
// General UI labels
// ============================================================
static const char EMPTY_STRING[] PROGMEM = "";
static const char GO_BACK[] PROGMEM = "Back";
static const char GO_HOME[] PROGMEM = "Home";
static const char CANCEL_STRING[] PROGMEM = "Cancel";
static const char STOP[] PROGMEM = "STOP";

// ============================================================
// Menu item names
// ============================================================
static const char OSSM_CONTROLLER_NAME[] PROGMEM = "Device Search";
static const char SETTINGS_NAME[] PROGMEM = "Settings";
static const char SLEEP_NAME[] PROGMEM = "Sleep";
static const char GO_BACK_NAME[] PROGMEM = "Go Back";
static const char WIFI_SETTINGS_NAME[] PROGMEM = "WiFi Settings";
static const char PAIRING_NAME[] PROGMEM = "Pairing";
static const char UPDATE_NAME[] PROGMEM = "Update Device";
static const char RESTART_NAME[] PROGMEM = "Restart Device";
static const char DEEP_SLEEP_NAME[] PROGMEM = "Sleep";

// ============================================================
// Device search & connection
// ============================================================
static const char DEVICE_SEARCH_TITLE[] PROGMEM = "Device Search";
static const char DEVICE_SEARCH_DESCRIPTION[] PROGMEM =
    "Searching for nearby devices...";
static const char NO_DEVICES_FOUND[] PROGMEM = "No devices found";
static const char UNKNOWN_DEVICE[] PROGMEM = "Unknown Device";
static const char CONNECTING_TITLE[] PROGMEM = "Connecting";
static const char CONNECTING_DESCRIPTION[] PROGMEM =
    "Connecting to device...";
static const char DEFAULT_OSSM_PATTERN_NAME[] PROGMEM = "Simple Stroke";
static const char DEFAULT_DEVICE_NAME[] PROGMEM = "Device";

// ============================================================
// Device stop
// ============================================================
static const char DEVICE_STOP_TITLE[] PROGMEM = "Device Stopped";
static const char DEVICE_STOP_DESCRIPTION[] PROGMEM =
    "Your device has been stopped and reset to default play settings; but it's "
    "still connected.";

// ============================================================
// WiFi
// ============================================================
static const char WIFI_SETTINGS_TITLE[] PROGMEM = "WiFi Settings";
static const char WIFI_SETTINGS_DESCRIPTION[] PROGMEM =
    "Join the network called 'RADR Setup' to configure WiFi on this "
    "device.";
static const char WIFI_SETTINGS_QR_VALUE[] PROGMEM =
    "WIFI:S:RADR Setup;T:nopass;;";
static const char WIFI_CONNECTED_TITLE[] PROGMEM = "Wi-Fi Connected";
static const char WIFI_CONNECTED_DESCRIPTION[] PROGMEM =
    "Your OSSM Remote is now connected to WiFi.";

// ============================================================
// Updates
// ============================================================
static const char UPDATING_TITLE[] PROGMEM = "Updating";
static const char UPDATING_DESCRIPTION[] PROGMEM = "Checking for updates...";
static const char UPDATING_FILESYSTEM_TITLE[] PROGMEM = "Updating Filesystem";
static const char UPDATING_FILESYSTEM_DESCRIPTION[] PROGMEM =
    "Updating filesystem...";
static const char UPDATING_SOFTWARE_TITLE[] PROGMEM = "Updating Software";
static const char UPDATING_SOFTWARE_DESCRIPTION[] PROGMEM =
    "Updating software...";
static const char UPDATE_COMPLETE_TITLE[] PROGMEM = "Update Complete";
static const char UPDATE_COMPLETE_DESCRIPTION[] PROGMEM = "Update complete!";

}  // namespace strings
}  // namespace ui

#endif  // UI_STRINGS_H
