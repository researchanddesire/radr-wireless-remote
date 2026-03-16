#ifndef UI_DISPLAY_TYPES_H
#define UI_DISPLAY_TYPES_H

#include <optional>
#include <string>
#include <vector>

#include "Progmem.h"

namespace ui {

enum class WifiStatus { DISCONNECTED, CONNECTED, ERROR };
enum class BleStatus { OFF, ON, SCANNING, CONNECTED };
enum class BatteryStatus { EMPTY, LOW_BATTERY, MID, FULL, CHARGING };

struct HeaderBarData {
    WifiStatus wifi = WifiStatus::DISCONNECTED;
    BleStatus ble = BleStatus::OFF;
    BatteryStatus battery = BatteryStatus::FULL;
};

struct TextPage {
    std::string title;
    std::string description;
    std::string qrValue;
    std::string leftButtonText;
    std::string rightButtonText;
    bool includeHeader = false;
};

enum MenuItemE {
    DEVICE_SEARCH,
    SETTINGS,
    SLEEP,
    RESTART,
    BACK,
    WIFI_SETTINGS,
    PAIRING,
    UPDATE,
    DEVICE_MENU_ITEM,
    DEEP_SLEEP
};

struct MenuItem {
    MenuItemE id;
    std::string name;
    const uint8_t *bitmap;
    std::optional<std::string> description = std::nullopt;
    int color = -1;
    int unfocusedColor = -1;
    int metaIndex = -1;
};

struct MenuData {
    const MenuItem *items;
    int count;
    int selectedIndex;
};

}  // namespace ui

#endif  // UI_DISPLAY_TYPES_H
