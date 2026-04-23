#ifndef OSSM_REMOTE_STATE_H
#define OSSM_REMOTE_STATE_H
#include "actions.hpp"
#include "boost/sml.hpp"
#include "events.hpp"
#include "guards.hpp"
#include "pages/controller.h"
#include "tasks/update.h"

namespace sml = boost::sml;

struct ossm_remote_state {
    auto operator()() const {
        using namespace sml;
        using namespace actions;

        return make_transition_table(
            // clang-format off
            *"init"_s + event<done> = "device_search"_s,

            "device_search"_s + on_entry<_> / (drawPage(deviceSearchPage), search, []() { setLed(LEDColors::logoBlue, 255, 1500); }),
            "device_search"_s + event<devices_found_event> = "device_list"_s,
            "device_search"_s + event<connected_event> = "device_draw_control"_s,
            "device_search"_s + event<left_button_pressed> / disconnect = "main_menu"_s,

            "device_list"_s + on_entry<_> / drawDeviceList,
            "device_list"_s + event<right_button_pressed> / selectDevice = "device_connecting"_s,
            "device_list"_s + event<left_button_pressed> / (disconnect, clearDeviceList) = "main_menu"_s,

            "device_connecting"_s + on_entry<_> / drawPage(deviceConnectingPage),
            "device_connecting"_s + event<connected_event> = "device_draw_control"_s,
            "device_connecting"_s + event<connected_error_event> = "device_list"_s,
            "device_connecting"_s + event<left_button_pressed> / disconnect = "device_list"_s,

            "main_menu"_s + on_entry<_> / drawMainMenu,
            "main_menu"_s + event<right_button_pressed>[isOption<>(MenuItemE::DEVICE_SEARCH)] = "device_search"_s,
            "main_menu"_s + event<right_button_pressed>[isOption<>(MenuItemE::SETTINGS)] = "settings_menu"_s,
            "main_menu"_s + event<right_button_pressed>[isOption<>(MenuItemE::DEEP_SLEEP)] = "deep_sleep"_s,
            "main_menu"_s + event<right_button_pressed>[isOption<>(MenuItemE::RESTART)] = "restart"_s,
            "main_menu"_s + event<connected_event> / start = "device_draw_control"_s,

            "settings_menu"_s + on_entry<_> / drawSettingsMenu,
            "settings_menu"_s + event<left_button_pressed> = "main_menu"_s,
            "settings_menu"_s + event<right_button_pressed>[isOption<>(MenuItemE::BACK)] = "main_menu"_s,
            "settings_menu"_s + event<right_button_pressed>[isOption<>(MenuItemE::WIFI_SETTINGS)] = "wmConfig"_s,
            "settings_menu"_s + event<right_button_pressed>[isOption<>(MenuItemE::UPDATE) && isOnline<>] = "update"_s,
            "settings_menu"_s + event<right_button_pressed>[isOption<>(MenuItemE::UPDATE)] = "update.wifi"_s,
            "settings_menu"_s + event<right_button_pressed>[isOption<>(MenuItemE::RESTART)] = "restart"_s,

            "update"_s + on_entry<_> / (drawPage(updatePage), startTask(updateTask, updateTaskName, updateTaskHandle)),
            "update"_s + event<done> [hasFilesystemUpdate<>] = "update.filesystem"_s,
            "update"_s + event<done> [hasSoftwareUpdate<>] = "update.software"_s,
            "update"_s + event<done> = "update.done"_s,
            "update.filesystem"_s + on_entry<_> / (drawPage(updateFilesystemPage), startTask(updateTask, updateTaskName, updateTaskHandle)),
            "update.filesystem"_s + event<done>[hasSoftwareUpdate<>] = "update.software"_s,
            "update.filesystem"_s + event<done> = "update.done"_s,
            "update.software"_s + on_entry<_> / (drawPage(updateSoftwarePage), startTask(updateTask, updateTaskName, updateTaskHandle)),
            "update.software"_s + event<done> = "restart"_s,
            "update.done"_s + on_entry<_> / (drawPage(updateDonePage)),
            "update.done"_s + event<left_button_pressed> = "restart"_s,
            "update.done"_s + event<right_button_pressed> = "restart"_s,

            
            "update.wifi"_s + on_entry<_> / (drawPage(wifiSettingsPage), startWiFiPortal),
            "update.wifi"_s + event<left_button_pressed> = "settings_menu"_s,
            "update.wifi"_s + event<wifi_connected>  = "update"_s,
            "update.wifi"_s + boost::sml::on_exit<_> / stopWiFiPortal,

            "wmConfig"_s + on_entry<_> / (drawPage(wifiSettingsPage), startWiFiPortal),
            "wmConfig"_s + event<left_button_pressed> = "settings_menu"_s,
            "wmConfig"_s + event<wifi_connected> / drawPage(wifiConnectedPage),
            "wmConfig"_s + boost::sml::on_exit<_> / stopWiFiPortal,

            "device_draw_control"_s + on_entry<_> / drawControl,
            "device_draw_control"_s + event<right_button_pressed>[hasDeviceMenu<>] = "device_menu"_s,
            // TODO: Left Menu button needs a menu behind it, this is disabled and only a placeholder for now.
            "device_draw_control"_s + event<left_button_pressed>[hasDeviceSettingsMenu<>] = "device_settings_menu"_s,
            "device_draw_control"_s + event<middle_button_pressed> / softPause,
            "device_draw_control"_s + event<middle_button_second_press> / stop = "device_stop"_s,
            "device_draw_control"_s + event<disconnected_event> / disconnect = "main_menu"_s,

            "device_menu"_s + on_entry<_> / drawDeviceMenu,
            "device_menu"_s + event<left_button_pressed> = "device_draw_control"_s,
            "device_menu"_s + event<right_button_pressed> / onDeviceMenuItemSelected = "device_draw_control"_s,
            "device_menu"_s + event<middle_button_pressed> / softPause,
            "device_menu"_s + event<middle_button_second_press> / stop = "device_stop"_s,
            "device_menu"_s + event<disconnected_event> / disconnect = "main_menu"_s,

            "device_settings_menu"_s + on_entry<_> / drawDeviceSettingsMenu,
            "device_settings_menu"_s + event<left_button_pressed> = "device_draw_control"_s,
            "device_settings_menu"_s + event<right_button_pressed> / onDeviceMenuItemSelected = "device_draw_control"_s,
            "device_settings_menu"_s + event<middle_button_pressed> / softPause,
            "device_settings_menu"_s + event<middle_button_second_press> / stop = "device_stop"_s,
            "device_settings_menu"_s + event<disconnected_event> / disconnect = "main_menu"_s,

            "device_stop"_s + on_entry<_> / (drawPage(deviceStopPage), stop),
            "device_stop"_s + event<right_button_pressed> / disconnect = "main_menu"_s,
            "device_stop"_s + event<left_button_pressed> / start = "device_draw_control"_s,
            "device_stop"_s + event<middle_button_pressed> / start = "device_draw_control"_s,
            "device_stop"_s + event<disconnected_event> / disconnect = "main_menu"_s,

            "restart"_s + on_entry<_> / espRestart,
            "restart"_s = X,

            "deep_sleep"_s + on_entry<_> / enterDeepSleep);

        // clang-format on
    }
};

#endif