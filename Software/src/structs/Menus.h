#ifndef LOCKBOX_MENUITEM_H
#define LOCKBOX_MENUITEM_H

#include <Arduino.h>

#include <vector>

#include "components/Icons.h"
#include <Strings.h>
// numeric enum for every menu item ever.
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
    const MenuItemE id;
    const std::string name;
    const uint8_t *bitmap;
    const std::optional<std::string> description = std::nullopt;

    // optional color defaults to -1;
    int color = -1;
    // optional unfocused color defaults to -1;
    int unfocusedColor = -1;

    // optional meta data index. Useful for dynamic device menus.
    int metaIndex = -1;
};

// MainMenu

static std::vector<MenuItem> mainMenu = {
    {MenuItemE::DEVICE_SEARCH, ui::strings::OSSM_CONTROLLER_NAME, researchAndDesireWaves},
    {MenuItemE::SETTINGS, ui::strings::SETTINGS_NAME, bitmap_settings},
    {MenuItemE::DEEP_SLEEP, ui::strings::DEEP_SLEEP_NAME, bitmap_sleep}};

static const int numMainMenu = mainMenu.size();

// SettingsMenu

static std::vector<MenuItem> settingsMenu = {
    {MenuItemE::BACK, ui::strings::GO_BACK_NAME, bitmap_back},
    {MenuItemE::WIFI_SETTINGS, ui::strings::WIFI_SETTINGS_NAME, bitmap_wifi},
    // {MenuItemE::PAIRING, ui::strings::PAIRING_NAME, bitmap_link},
    {MenuItemE::UPDATE, ui::strings::UPDATE_NAME, bitmap_update},
    {MenuItemE::RESTART, ui::strings::RESTART_NAME, bitmap_restart},
};

static const int numSettingsMenu = settingsMenu.size();

#endif