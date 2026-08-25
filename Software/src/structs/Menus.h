#ifndef LOCKBOX_MENUITEM_H
#define LOCKBOX_MENUITEM_H

#include <Arduino.h>

#include <optional>
#include <string>
#include <vector>

#include "components/Icons.h"
#include "constants/Colors.h"
#include "constants/Strings.h"
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
    DEEP_SLEEP,
    OSSM_STROKE_ENGINE,
    OSSM_SIMPLE_PENETRATION,
    OSSM_STREAMING,
    OSSM_PAIRING,
    OSSM_UPDATE,
    OSSM_HELP,
    OSSM_RESTART
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

inline std::vector<MenuItem> mainMenu = {
    {MenuItemE::DEVICE_SEARCH, OSSM_CONTROLLER_NAME, researchAndDesireWaves},
    {MenuItemE::SETTINGS, SETTINGS_NAME, bitmap_settings},
    {MenuItemE::DEEP_SLEEP, DEEP_SLEEP_NAME, bitmap_sleep}};

inline const int numMainMenu = mainMenu.size();

// SettingsMenu

inline std::vector<MenuItem> settingsMenu = {
    {MenuItemE::BACK, GO_BACK_NAME, bitmap_back},
    {MenuItemE::WIFI_SETTINGS, WIFI_SETTINGS_NAME, bitmap_wifi},
    // {MenuItemE::PAIRING, PAIRING_NAME, bitmap_link},
    {MenuItemE::UPDATE, UPDATE_NAME, bitmap_update},
    {MenuItemE::RESTART, RESTART_NAME, bitmap_restart},
};

inline const int numSettingsMenu = settingsMenu.size();

// OSSM Menu (shown in OSSM tab when connected)

inline std::vector<MenuItem> ossmMenu = {
    {MenuItemE::OSSM_STROKE_ENGINE, "Stroke Engine", researchAndDesireWaves},
    {MenuItemE::OSSM_SIMPLE_PENETRATION, "Simple Penetration",
     researchAndDesireWaves},
    {MenuItemE::OSSM_STREAMING, "Streaming", researchAndDesireTerminal},
    {MenuItemE::OSSM_PAIRING, "Pairing", bitmap_link},
    {MenuItemE::OSSM_UPDATE, "Update OSSM", bitmap_update},
    {MenuItemE::OSSM_HELP, "Help", researchAndDesireHeart},
    {MenuItemE::OSSM_RESTART, "Restart OSSM", bitmap_restart},
};

inline const int numOssmMenu = ossmMenu.size();

#endif
