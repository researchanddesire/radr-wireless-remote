#include "events.hpp"
#include "services/encoder.h"
#include "tasks/update.h"

// template <typename Event>
// constexpr bool is_valid(const Event &event)
// {
//     return event.valid;
// }

// template <typename Event>
//
template <typename Event>
const auto is_valid = [](const Event &event) {
    ESP_LOGI("TEST", "is_valid");
    return true;
};

template <typename Event = done>
const auto hasFilesystemUpdate =
    [](const Event &event) { return isFilesystemUpdateAvailable; };

template <typename Event = done>
const auto hasSoftwareUpdate =
    [](const Event &event) { return isSoftwareUpdateAvailable; };

template <typename Event = right_button_pressed>
const auto isOnline =
    [](const Event &event) { return WiFi.status() == WL_CONNECTED; };

template <typename Event = right_button_pressed>
auto isOption = [](MenuItemE value) {
    return [value](const Event &event) -> bool {
        auto currentOption = rightEncoder.readEncoder();
        auto indexOfValue = -1;

        for (int i = 0; i < activeMenuCount; i++) {
            if (activeMenu->at(i).id == value) {
                indexOfValue = i;
                break;
            }
        }

        bool result = currentOption == indexOfValue;
        return result;
    };
};

template <typename Event = right_button_pressed>
auto hasDeviceMenu = [](const Event &event) -> bool {
    return device != nullptr && device->menu.size() > 0;
};
const auto hasDeviceSettingsMenu = []() -> bool {
    return device != nullptr && device->settingsMenu.size() > 0;
};

const auto isPaused = []() -> bool {
    return device != nullptr && device->isPaused;
};

const auto isConnected = []() -> bool {
    return device != nullptr && device->isConnected;
};

const auto isSimplePenetrationMode = []() -> bool {
    return device != nullptr && device->isInSimplePenetrationMode();
};

// Declared in ossmUpdate.cpp
extern bool ossmUpdateIsAvailable;

template <typename Event = done>
const auto hasOssmUpdate =
    [](const Event &event) { return ossmUpdateIsAvailable; };
