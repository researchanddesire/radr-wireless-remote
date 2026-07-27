#include "rad_ble.h"

#include <LittleFS.h>
#include <WiFi.h>

#include "constants/Version.h"
#include "devices/device.h"
#include "devices/registry.h"
#include "display.h"
#include "encoder.h"
#include "imu.h"
#include "lastInteraction.h"
#include "leds.h"
#include "memory.h"
#include "battery.h"
#include "buzzer.h"
#include "coms.h"
#include "vibrator.h"
#include "pages/menus.h"
#include "state/events.hpp"
#include "state/remote.h"

#ifndef FIRMWARE_BUILD_SHA
#define FIRMWARE_BUILD_SHA "unknown"
#endif

namespace {

constexpr uint16_t R = radble::RESOURCE_READABLE |
                       radble::RESOURCE_AVAILABLE;
constexpr uint16_t RW = R | radble::RESOURCE_WRITABLE |
                        radble::RESOURCE_LEASE_REQUIRED;
constexpr uint16_t RWP = RW | radble::RESOURCE_PERSISTENT;
constexpr uint16_t RS = R | radble::RESOURCE_STREAMABLE;
constexpr uint16_t RWT = RW | radble::RESOURCE_STREAMABLE;
constexpr uint16_t W = radble::RESOURCE_WRITABLE |
                       radble::RESOURCE_AVAILABLE |
                       radble::RESOURCE_LEASE_REQUIRED;

radble::Resource RESOURCES[] = {
    {"device_name", "device.name", "setting", "string", "", RWP,
     "{\"maxBytes\":24,\"emptyResets\":true}"},
    {"left_shoulder", "button.leftShoulder", "button", "bool", "", RS,
     "{\"events\":[\"click\"]}"},
    {"right_shoulder", "button.rightShoulder", "button", "bool", "", RS,
     "{\"events\":[\"click\"]}"},
    {"left", "button.left", "button", "bool", "", RS,
     "{\"events\":[\"click\"]}"},
    {"middle", "button.middle", "button", "bool", "", RS,
     "{\"events\":[\"click\",\"long\"]}"},
    {"right", "button.right", "button", "bool", "", RS,
     "{\"events\":[\"click\"]}"},
    {"left_encoder", "encoder.left", "encoder", "int", "percent", RWT,
     "{\"min\":0,\"max\":100}"},
    {"right_encoder", "encoder.right", "encoder", "int", "percent", RWT,
     "{\"min\":0,\"max\":100}"},
    {"left_encoder_changed", "encoder.leftChanged", "encoder", "bool", "",
     RS, "{\"raw\":true}"},
    {"right_encoder_changed", "encoder.rightChanged", "encoder", "bool", "",
     RS, "{\"raw\":true}"},
    {"accelerometer", "imu.acceleration", "imu", "vector3", "g", RS, ""},
    {"gyroscope", "imu.gyroscope", "imu", "vector3", "dps", RS, ""},
    {"temperature", "imu.temperature", "imu", "float", "C", RS, ""},
    {"battery_percent", "power.batteryPercent", "power", "int", "percent", RS,
     "{\"min\":0,\"max\":100}"},
    {"battery_voltage", "power.batteryVoltage", "power", "float", "V", RS,
     ""},
    {"charging", "power.charging", "power", "bool", "", RS, ""},
    {"charge_rate", "power.chargeRate", "power", "float", "V/s", RS, ""},
    {"ble_scan", "connectivity.bleScan", "connectivity", "event", "", RW,
     "{\"timeoutMs\":5000}"},
    {"wifi", "connectivity.wifi", "connectivity", "object", "", RS, ""},
    {"peripheral", "connectivity.peripheral", "connectivity", "object", "",
     RS, ""},
    {"ble_central", "connectivity.bleCentral", "connectivity", "object", "",
     RS, ""},
    {"ble_peripheral", "connectivity.blePeripheral", "connectivity", "object",
     "", RS, ""},
    {"device_registry", "setting.deviceRegistry", "setting", "object", "", R,
     "{\"destructiveWrites\":false}"},
    {"memory", "system.memoryPresent", "setting", "bool", "", R, ""},
    {"idle", "system.idleState", "setting", "int", "enum", R, ""},
    {"last_interaction", "system.lastInteractionMs", "setting", "uint32", "ms",
     R, ""},
    {"sleep_duration", "system.sleepDurationMs", "setting", "uint32", "ms", R,
     ""},
    {"idle_timeout", "setting.idleTimeoutMs", "setting", "uint32", "ms", R,
     ""},
    {"pseudo_sleep_timeout", "setting.pseudoSleepTimeoutMs", "setting",
     "uint32", "ms", R, ""},
    {"sleep_timeout", "setting.sleepTimeoutMs", "setting", "uint32", "ms", R,
     ""},
    {"ossm_speed", "peripheral.ossm.speed", "setting", "int", "percent", RW,
     "{\"min\":0,\"max\":100,\"validStates\":[\"device_draw_control\",\"simple_penetration_control\"]}"},
    {"ossm_stroke", "peripheral.ossm.stroke", "setting", "int", "percent", RW,
     "{\"min\":0,\"max\":100,\"validStates\":[\"device_draw_control\",\"simple_penetration_control\"]}"},
    {"ossm_depth", "peripheral.ossm.depth", "setting", "int", "percent", RW,
     "{\"min\":0,\"max\":100,\"validStates\":[\"device_draw_control\"]}"},
    {"ossm_sensation", "peripheral.ossm.sensation", "setting", "int",
     "percent", RW,
     "{\"min\":0,\"max\":100,\"validStates\":[\"device_draw_control\"]}"},
    {"ossm_pattern", "peripheral.ossm.pattern", "setting", "int", "index", RW,
     "{\"min\":0}"},
    {"led", "indicator.status", "indicator", "object", "hsv", RW, ""},
    {"led_left", "indicator.left", "indicator", "object", "rgb565", RW, ""},
    {"led_middle", "indicator.middle", "indicator", "object", "rgb565", RW,
     ""},
    {"led_right", "indicator.right", "indicator", "object", "rgb565", RW,
     ""},
    {"vibrator", "haptic.vibrator", "haptic", "string", "pattern", RW,
     "{\"patterns\":[\"single\",\"double\",\"triple\",\"error\",\"stop\"]}"},
    {"buzzer", "audio.buzzer", "audio", "string", "pattern", RW,
     "{\"patterns\":[\"single\",\"double\",\"triple\",\"error\",\"radar\",\"stop\"]}"},
    {"backlight", "display.backlight", "display", "int", "raw", RW,
     "{\"min\":0,\"max\":255}"},
    {"target_main", "state.mainMenu", "state", "event", "", W, ""},
    {"target_search", "state.deviceSearch", "state", "event", "", W, ""},
    {"target_connect", "state.connectSelected", "state", "event", "", W,
     "{\"args\":[\"index\"]}"},
    {"target_disconnect", "state.disconnect", "state", "event", "", W, ""},
    {"target_settings", "state.settings", "state", "event", "", W, ""},
    {"target_device_menu", "state.deviceMenu", "state", "event", "", W, ""},
    {"target_control", "state.deviceControl", "state", "event", "", W, ""},
    {"target_pause", "state.pause", "state", "event", "", W, ""},
    {"target_resume", "state.resume", "state", "event", "", W, ""},
    {"target_stop", "state.stop", "state", "event", "", W, ""},
    {"target_wifi", "state.wifiSetup", "state", "event", "", W, ""},
    {"target_update", "state.localUpdate", "state", "event", "", W, ""},
    {"target_sleep", "state.sleep", "state", "event", "", W, ""},
    {"target_restart", "state.restart", "state", "event", "", W, ""},
    {"target_stroke_engine", "state.ossm.strokeEngine", "state", "event", "",
     W, ""},
    {"target_simple", "state.ossm.simplePenetration", "state", "event", "", W,
     ""},
    {"target_streaming", "state.ossm.streaming", "state", "event", "", W, ""},
    {"target_pairing", "state.ossm.pairing", "state", "event", "", W, ""},
    {"target_ossm_update", "state.ossm.update", "state", "event", "", W, ""},
    {"target_help", "state.ossm.help", "state", "event", "", W, ""},
    {"target_ossm_restart", "state.ossm.restart", "state", "event", "", W,
     ""},
    {"event_done", "event.done", "event", "event", "", W, ""},
    {"event_wifi", "event.wifiConnected", "event", "event", "", W, ""},
    {"event_connected", "event.connected", "event", "event", "", W, ""},
    {"event_connect_error", "event.connectedError", "event", "event", "", W,
     ""},
    {"event_disconnected", "event.disconnected", "event", "event", "", W,
     ""},
    {"event_devices", "event.devicesFound", "event", "event", "", W, ""},
    {"event_wake", "event.wake", "event", "event", "", W, ""},
};

String stateName() {
    String state = "starting";
    if (stateMachine != nullptr)
        stateMachine->visit_current_states(
            [&state](auto current) { state = current.c_str(); });
    return state;
}

bool selectActiveMenuItem(MenuItemE item) {
    if (activeMenu == nullptr || stateMachine == nullptr) return false;
    for (int index = 0; index < activeMenuCount; ++index) {
        if (activeMenu->at(index).id != item) continue;
        currentOption = index;
        rightEncoder.setEncoderValue(index);
        return stateMachine->process_event(right_button_pressed{});
    }
    return false;
}

void selectRadrMenuSurface() {
    activeTab = 1;
    activeMenu = &mainMenu;
    activeMenuCount = numMainMenu;
    currentOption = 0;
}

bool selectOssmMenuItem(MenuItemE item) {
    if (device == nullptr || !device->isConnected) return false;
    activeTab = 0;
    activeMenu = &ossmMenu;
    activeMenuCount = numOssmMenu;
    currentOption = 0;
    return selectActiveMenuItem(item);
}

bool returnToMainMenu() {
    if (stateMachine == nullptr) return false;
    for (uint8_t attempt = 0; attempt < 4; ++attempt) {
        const String state = stateName();
        if (state == "main_menu") return true;
        bool accepted = false;
        if (state == "settings_menu" || state == "device_search" ||
            state == "device_list" || state == "device_connecting" ||
            state == "streaming_screen" || state == "ossm_help" ||
            state == "ossm_restart_confirm" ||
            state == "ossm_restarting" || state == "ossm_pairing" ||
            state == "ossm_pairing_success" ||
            state == "ossm_pairing_wifi" ||
            state == "ossm_update_check" ||
            state == "ossm_update_confirm" ||
            state == "ossm_update_updating" ||
            state == "ossm_update_none" || state == "ossm_update_wifi" ||
            state == "wmConfig" || state == "update.wifi") {
            accepted = stateMachine->process_event(left_button_pressed{});
        } else if (state == "device_menu") {
            accepted = stateMachine->process_event(left_button_pressed{});
        } else if (state == "device_draw_control" ||
                   state == "simple_penetration_control") {
            if (device != nullptr && !device->isPaused)
                accepted =
                    stateMachine->process_event(middle_button_pressed{});
            else
                accepted = stateMachine->process_event(left_button_pressed{});
        } else if (state == "device_stop" ||
                   state == "simple_penetration_stop") {
            accepted = stateMachine->process_event(right_button_pressed{});
        }
        if (!accepted) return false;
    }
    return stateName() == "main_menu";
}

radble::Result targetResult(const String& target, bool accepted) {
    if (!accepted)
        return radble::Result::failure(
            "guard_rejected", "The current state rejected this target");
    return radble::Result::success("{\"target\":\"" + target +
                                   "\",\"state\":\"" + stateName() +
                                   "\"}");
}

radble::Result requestTarget(const String& path, JsonObjectConst args) {
    if (stateMachine == nullptr)
        return radble::Result::failure("not_ready", "RADR is still starting");
    if (path == "state.mainMenu")
        return targetResult(path, returnToMainMenu());
    if (path == "state.deviceSearch") {
        if (!returnToMainMenu()) return targetResult(path, false);
        selectRadrMenuSurface();
        return targetResult(path, selectActiveMenuItem(MenuItemE::DEVICE_SEARCH));
    }
    if (path == "state.connectSelected") {
        if (stateName() != "device_list")
            return radble::Result::failure(
                "invalid_state", "A completed device scan is required first");
        if (!args["index"].is<int>())
            return radble::Result::failure("invalid_value",
                                           "A device-list index is required");
        const int index = args["index"].as<int>();
        if (index < 0 || index >= static_cast<int>(getDiscoveredDevices().size()))
            return radble::Result::failure("invalid_value",
                                           "Device-list index is out of range");
        currentOption = index;
        rightEncoder.setEncoderValue(index);
        return targetResult(
            path, stateMachine->process_event(right_button_pressed{}));
    }
    if (path == "state.disconnect") {
        if (device == nullptr) return targetResult(path, true);
        if (stateName() == "device_connecting")
            return targetResult(
                path, stateMachine->process_event(left_button_pressed{}));
        return targetResult(
            path, stateMachine->process_event(disconnected_event{}));
    }
    if (path == "state.settings") {
        if (!returnToMainMenu()) return targetResult(path, false);
        selectRadrMenuSurface();
        return targetResult(path, selectActiveMenuItem(MenuItemE::SETTINGS));
    }
    if (path == "state.deviceMenu") {
        if (stateName() != "device_draw_control")
            return radble::Result::failure(
                "invalid_state", "Device controls must be active");
        return targetResult(
            path, stateMachine->process_event(right_button_pressed{}));
    }
    if (path == "state.deviceControl") {
        const String state = stateName();
        if (state == "device_draw_control" ||
            state == "simple_penetration_control")
            return targetResult(path, true);
        if (device == nullptr || !device->isConnected)
            return radble::Result::failure("invalid_state",
                                           "No peripheral is connected");
        if (state == "device_menu" || state == "device_stop" ||
            state == "simple_penetration_stop")
            return targetResult(
                path, stateMachine->process_event(left_button_pressed{}));
        if (state == "main_menu")
            return targetResult(
                path, stateMachine->process_event(left_button_pressed{}));
        return targetResult(path, false);
    }
    if (path == "state.pause") {
        const String state = stateName();
        if ((state != "device_draw_control" &&
             state != "simple_penetration_control") || device == nullptr)
            return radble::Result::failure("invalid_state",
                                           "Device controls must be active");
        if (device->isPaused) return targetResult(path, true);
        return targetResult(
            path, stateMachine->process_event(middle_button_pressed{}));
    }
    if (path == "state.resume") {
        const String state = stateName();
        if (state == "device_stop" || state == "simple_penetration_stop")
            return targetResult(
                path, stateMachine->process_event(left_button_pressed{}));
        if ((state != "device_draw_control" &&
             state != "simple_penetration_control") || device == nullptr)
            return radble::Result::failure("invalid_state",
                                           "Device controls must be active");
        if (!device->isPaused) return targetResult(path, true);
        return targetResult(
            path, stateMachine->process_event(middle_button_pressed{}));
    }
    if (path == "state.stop") {
        if (stateName() != "device_draw_control")
            return radble::Result::failure(
                "invalid_state", "Stroke Engine controls must be active");
        return targetResult(
            path, stateMachine->process_event(middle_button_long_pressed{}));
    }

    MenuItemE item;
    bool settingsItem = false;
    bool ossmItem = false;
    if (path == "state.wifiSetup") {
        item = MenuItemE::WIFI_SETTINGS;
        settingsItem = true;
    } else if (path == "state.localUpdate") {
        item = MenuItemE::UPDATE;
        settingsItem = true;
    } else if (path == "state.restart") {
        item = MenuItemE::RESTART;
        settingsItem = true;
    } else if (path == "state.sleep") {
        item = MenuItemE::DEEP_SLEEP;
    } else if (path == "state.ossm.strokeEngine") {
        item = MenuItemE::OSSM_STROKE_ENGINE;
        ossmItem = true;
    } else if (path == "state.ossm.simplePenetration") {
        item = MenuItemE::OSSM_SIMPLE_PENETRATION;
        ossmItem = true;
    } else if (path == "state.ossm.streaming") {
        item = MenuItemE::OSSM_STREAMING;
        ossmItem = true;
    } else if (path == "state.ossm.pairing") {
        item = MenuItemE::OSSM_PAIRING;
        ossmItem = true;
    } else if (path == "state.ossm.update") {
        item = MenuItemE::OSSM_UPDATE;
        ossmItem = true;
    } else if (path == "state.ossm.help") {
        item = MenuItemE::OSSM_HELP;
        ossmItem = true;
    } else if (path == "state.ossm.restart") {
        item = MenuItemE::OSSM_RESTART;
        ossmItem = true;
    } else {
        return radble::Result::failure("unknown_path", "Unknown state target");
    }

    if (settingsItem) {
        if (stateName() != "settings_menu") {
            if (!returnToMainMenu()) return targetResult(path, false);
            selectRadrMenuSurface();
            if (!selectActiveMenuItem(MenuItemE::SETTINGS))
                return targetResult(path, false);
        }
        return targetResult(path, selectActiveMenuItem(item));
    }
    if (!returnToMainMenu()) return targetResult(path, false);
    if (ossmItem) return targetResult(path, selectOssmMenuItem(item));
    selectRadrMenuSurface();
    return targetResult(path, selectActiveMenuItem(item));
}

void requestNetworkOtaTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (stateMachine == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    String state = stateName();
    if (state == "main_menu") {
        selectRadrMenuSurface();
        selectActiveMenuItem(MenuItemE::SETTINGS);
        vTaskDelay(pdMS_TO_TICKS(20));
        state = stateName();
    }
    if (state == "settings_menu") {
        selectActiveMenuItem(MenuItemE::UPDATE);
    }
    vTaskDelete(nullptr);
}

enum class DeferredLocalTarget : uintptr_t { Sleep, Restart };

void requestLocalPowerTargetTask(void* parameter) {
    vTaskDelay(pdMS_TO_TICKS(500));
    const auto target = static_cast<DeferredLocalTarget>(
        reinterpret_cast<uintptr_t>(parameter));
    if (target == DeferredLocalTarget::Restart) {
        if (stateName() == "main_menu") {
            selectRadrMenuSurface();
            selectActiveMenuItem(MenuItemE::SETTINGS);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (stateName() == "settings_menu")
            selectActiveMenuItem(MenuItemE::RESTART);
    } else if (stateName() == "main_menu") {
        selectRadrMenuSurface();
        selectActiveMenuItem(MenuItemE::DEEP_SLEEP);
    }
    vTaskDelete(nullptr);
}

bool emitButton(const String& path, const String& event) {
    if (path == "button.leftShoulder" && event == "click") {
        if (stateName() == "main_menu") {
            if (device == nullptr || !device->isConnected) return false;
            activeTab = 0;
            activeMenu = &ossmMenu;
            activeMenuCount = numOssmMenu;
            currentOption = 0;
            return true;
        }
        const String state = stateName();
        if ((state != "device_draw_control" &&
             state != "simple_penetration_control") ||
            device == nullptr || !device->isConnected)
            return false;
        device->onLeftBumperClick();
        return true;
    } else if (path == "button.rightShoulder" && event == "click") {
        if (stateName() == "main_menu") {
            selectRadrMenuSurface();
            return true;
        }
        const String state = stateName();
        if ((state != "device_draw_control" &&
             state != "simple_penetration_control") ||
            device == nullptr || !device->isConnected)
            return false;
        device->onRightBumperClick();
        return true;
    }
    else if (path == "button.left" && event == "click")
        return stateMachine->process_event(left_button_pressed{});
    else if (path == "button.middle" && event == "click")
        return stateMachine->process_event(middle_button_pressed{});
    else if (path == "button.middle" && event == "long")
        return stateMachine->process_event(middle_button_long_pressed{});
    else if (path == "button.right" && event == "click")
        return stateMachine->process_event(right_button_pressed{});
    return false;
}

radble::Result handleCommand(JsonObjectConst request, void*) {
    const String operation = request["op"] | "";
    const String path = request["path"] | "";
    const JsonObjectConst args = request["args"].as<JsonObjectConst>();

    if (operation == "ota.start" && String(args["transport"] | "") == "wifi") {
        if (WiFi.status() != WL_CONNECTED)
            return radble::Result::failure("network_failed", "Wi-Fi is disconnected");
        if (device != nullptr && device->isConnected)
            return radble::Result::failure(
                "busy", "Disconnect the controlled device before network OTA");
        const String state = stateName();
        if (state != "main_menu" && state != "settings_menu")
            return radble::Result::failure(
                "invalid_state", "Open the RADR main or settings menu first");
        NimBLEDevice::getScan()->stop();
        if (xTaskCreate(requestNetworkOtaTask, "rad-net-ota", 2048, nullptr, 1,
                        nullptr) != pdPASS)
            return radble::Result::failure("busy", "Could not schedule network OTA");
        return radble::Result::success(R"({"transport":"wifi","requested":true})");
    }

    if (operation == "sensor.read" || operation == "setting.read") {
        JsonDocument document;
        document["path"] = path;
        if (path == "button.leftShoulder")
            document["value"] = digitalRead(pins::BTN_L_SHOULDER) == LOW;
        else if (path == "button.rightShoulder")
            document["value"] = digitalRead(pins::BTN_R_SHOULDER) == LOW;
        else if (path == "button.left")
            document["value"] = digitalRead(pins::BTN_UNDER_L) == LOW;
        else if (path == "button.middle")
            document["value"] = digitalRead(pins::BTN_UNDER_C) == LOW;
        else if (path == "button.right")
            document["value"] = digitalRead(pins::BTN_UNDER_R) == LOW;
        else if (path == "encoder.left")
            document["value"] = leftEncoder.readEncoder();
        else if (path == "encoder.right")
            document["value"] = rightEncoder.readEncoder();
        else if (path == "encoder.leftChanged")
            document["value"] = hasLeftEncoderChanged(false);
        else if (path == "encoder.rightChanged")
            document["value"] = hasRightEncoderChanged(false);
        else if (path == "power.batteryPercent")
            document["value"] = getBatteryPercent();
        else if (path == "power.batteryVoltage")
            document["value"] = getBatteryVoltage();
        else if (path == "power.charging")
            document["value"] = isCharging();
        else if (path == "power.chargeRate")
            document["value"] = lastChargeRate;
        else if (path == "system.memoryPresent")
            document["value"] = isMemoryChipFound;
        else if (path == "system.idleState")
            document["value"] = static_cast<int>(idleState);
        else if (path == "system.lastInteractionMs")
            document["value"] = lastInteraction;
        else if (path == "system.sleepDurationMs")
            document["value"] = sleepDuration;
        else if (path == "setting.idleTimeoutMs")
            document["value"] = IDLE_TIMEOUT;
        else if (path == "setting.pseudoSleepTimeoutMs")
            document["value"] = PSEUDO_SLEEP_TIMEOUT;
        else if (path == "setting.sleepTimeoutMs")
            document["value"] = SLEEP_TIMEOUT;
        else if (path == "connectivity.bleScan") {
            document["scanning"] = NimBLEDevice::getScan()->isScanning();
            document["discovered"] = getDiscoveredDevices().size();
        } else if (path == "connectivity.wifi") {
            document["connected"] = WiFi.status() == WL_CONNECTED;
            document["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
            document["ip"] = WiFi.localIP().toString();
        } else if (path == "connectivity.bleCentral") {
            document["connectedClients"] =
                NimBLEDevice::getConnectedClients().size();
            document["createdClients"] = NimBLEDevice::getCreatedClientCount();
            document["scanning"] = NimBLEDevice::getScan()->isScanning();
        } else if (path == "connectivity.blePeripheral") {
            NimBLEServer* server = NimBLEDevice::getServer();
            document["connectedClients"] =
                server == nullptr ? 0 : server->getConnectedCount();
        } else if (path == "connectivity.peripheral") {
            document["connected"] = device != nullptr && device->isConnected;
            if (device != nullptr && device->advertisedDevice != nullptr) {
                document["name"] = device->getName();
                document["rssi"] = device->advertisedDevice->getRSSI();
            }
        } else if (path == "setting.deviceRegistry") {
            document["count"] = registry.size();
            JsonArray entries = document["entries"].to<JsonArray>();
            size_t count = 0;
            for (const auto& entry : registry) {
                if (count++ == 4) break;
                entries.add(entry.first.c_str());
            }
            document["truncated"] = registry.size() > 4;
        } else if (path.startsWith("peripheral.ossm.")) {
            if (device == nullptr || !device->isConnected)
                return radble::Result::failure("invalid_state",
                                               "No peripheral is connected");
            const String name = path.substring(strlen("peripheral.ossm."));
            float value = 0;
            if (!device->readControlSetting(name, value))
                return radble::Result::failure(
                    "unsupported", "The connected peripheral lacks this setting");
            document["value"] = value;
        } else if (path.startsWith("imu.")) {
            updateIMUReadings();
            const IMUSnapshot imu = getIMUSnapshot();
            document["available"] = imu.available;
            if (path == "imu.acceleration") {
                document["x"] = imu.accelX;
                document["y"] = imu.accelY;
                document["z"] = imu.accelZ;
            } else if (path == "imu.gyroscope") {
                document["x"] = imu.gyroX;
                document["y"] = imu.gyroY;
                document["z"] = imu.gyroZ;
            } else if (path == "imu.temperature") {
                document["value"] = imu.temperatureC;
            } else {
                return radble::Result::failure("unknown_path", "Unknown IMU path");
            }
        } else
            return radble::Result::failure("unknown_path", "Unknown resource path");
        String output;
        serializeJson(document, output);
        return radble::Result::success(output);
    }

    if (operation == "input.emit" || operation == "event.emit") {
        if (stateMachine == nullptr)
            return radble::Result::failure("not_ready", "RADR is still starting");
        if (operation == "event.emit" && path.startsWith("event.")) {
            bool known = true;
            bool accepted = false;
            if (path == "event.done")
                accepted = stateMachine->process_event(done{});
            else if (path == "event.wifiConnected")
                accepted = stateMachine->process_event(wifi_connected{});
            else if (path == "event.connected") {
                if (device == nullptr || !device->isConnected)
                    return radble::Result::failure(
                        "invalid_state", "No peripheral is connected");
                accepted = stateMachine->process_event(connected_event{});
            } else if (path == "event.connectedError")
                accepted = stateMachine->process_event(connected_error_event{});
            else if (path == "event.disconnected")
                accepted = stateMachine->process_event(disconnected_event{});
            else if (path == "event.devicesFound") {
                if (getDiscoveredDevices().empty())
                    return radble::Result::failure(
                        "invalid_state", "The current scan has no devices");
                accepted = stateMachine->process_event(devices_found_event{});
            } else if (path == "event.wake")
                accepted = stateMachine->process_event(wake_up_event{});
            else
                known = false;
            if (!known)
                return radble::Result::failure("unknown_path",
                                               "Unknown state-machine event");
            if (!accepted)
                return radble::Result::failure(
                    "guard_rejected", "The current state rejected this event");
            Serial.printf("[RAD BLE][RADR] event %s\n", path.c_str());
            return radble::Result::success();
        }
        const String event = args["event"] | "click";
        const bool knownEvent =
            (path == "button.middle" &&
             (event == "click" || event == "long")) ||
            ((path == "button.leftShoulder" ||
              path == "button.rightShoulder" || path == "button.left" ||
              path == "button.right") &&
             event == "click");
        if (!knownEvent) {
            if (path.startsWith("button."))
                return radble::Result::failure("invalid_value",
                                               "Unknown button event");
            return radble::Result::failure("unknown_path",
                                           "Unknown button path");
        }
        if (!emitButton(path, event))
            return radble::Result::failure(
                "guard_rejected", "The current state rejected this event");
        Serial.printf("[RAD BLE][RADR] input %s %s\n", path.c_str(),
                      event.c_str());
        return radble::Result::success();
    }

    if ((operation == "encoder.set" || operation == "encoder.delta" ||
         operation == "setting.write") &&
        (path == "encoder.left" || path == "encoder.right")) {
        if ((operation == "encoder.delta" && !args["delta"].is<int>()) ||
            (operation != "encoder.delta" && !args["value"].is<int>()))
            return radble::Result::failure("invalid_value",
                                           "An integer encoder value is required");
        const int current = path == "encoder.left" ? leftEncoder.readEncoder()
                                                     : rightEncoder.readEncoder();
        const int requested = operation == "encoder.delta"
                                  ? current + (args["delta"] | 0)
                                  : (args["value"] | 0);
        if (requested < 0 || requested > 100)
            return radble::Result::failure("invalid_value",
                                           "Encoder value must be 0..100");
        const int value = requested;
        const String state = stateName();
        const bool controlsActive =
            state == "device_draw_control" ||
            state == "simple_penetration_control";
        if (path == "encoder.left") {
            leftEncoder.setEncoderValue(value);
            if (controlsActive && device != nullptr)
                device->onLeftEncoderChange(value);
        } else {
            rightEncoder.setEncoderValue(value);
            if (controlsActive && device != nullptr)
                device->onRightEncoderChange(value);
        }
        Serial.printf("[RAD BLE][RADR] %s=%d\n", path.c_str(), value);
        return radble::Result::success("{\"value\":" + String(value) + "}");
    }

    if (operation == "setting.write" &&
        path.startsWith("peripheral.ossm.")) {
        if (!args["value"].is<int>())
            return radble::Result::failure("invalid_value",
                                           "An integer setting is required");
        const int value = args["value"].as<int>();
        if (value < 0 || value > 100)
            return radble::Result::failure("invalid_value",
                                           "Control settings must be 0..100");
        if (device == nullptr || !device->isConnected)
            return radble::Result::failure("invalid_state",
                                           "No peripheral is connected");
        const String state = stateName();
        if (state != "device_draw_control" &&
            state != "simple_penetration_control" &&
            !(path == "peripheral.ossm.pattern" && state == "device_menu"))
            return radble::Result::failure(
                "invalid_state", "OSSM controls are not active");
        const String name = path.substring(strlen("peripheral.ossm."));
        if (!device->writeControlSetting(name, value))
            return radble::Result::failure(
                "hardware_fault", "The peripheral rejected the setting");
        return radble::Result::success("{\"value\":" + String(value) + "}");
    }

    if (operation == "target.set" &&
        (path == "state.restart" || path == "state.sleep")) {
        const String state = stateName();
        const bool allowed = path == "state.restart"
                                 ? state == "main_menu" ||
                                       state == "settings_menu"
                                 : state == "main_menu";
        if (!allowed)
            return radble::Result::failure(
                "invalid_state", "Open the required menu before power control");
        const auto target = path == "state.restart"
                                ? DeferredLocalTarget::Restart
                                : DeferredLocalTarget::Sleep;
        if (xTaskCreate(requestLocalPowerTargetTask, "rad-power", 2048,
                        reinterpret_cast<void*>(
                            static_cast<uintptr_t>(target)),
                        1, nullptr) != pdPASS)
            return radble::Result::failure("busy",
                                           "Could not schedule power control");
        return radble::Result::success(R"({"requested":true})");
    }

    if (operation == "target.set" && path.startsWith("state."))
        return requestTarget(path, args);

    if (operation == "target.set" && path == "connectivity.bleScan") {
        if (!args["timeoutMs"].isNull() && !args["timeoutMs"].is<int>())
            return radble::Result::failure("invalid_value",
                                           "Scan timeout must be an integer");
        const int timeoutMs = args["timeoutMs"] | 5000;
        if (timeoutMs < 1000 || timeoutMs > 30000)
            return radble::Result::failure("invalid_value",
                                           "Scan timeout must be 1000..30000 ms");
        if (device != nullptr && device->isConnected)
            return radble::Result::failure(
                "busy", "Disconnect the controlled peripheral before scanning");
        if (!startScanWithTimeout(timeoutMs, nullptr))
            return radble::Result::failure("hardware_fault",
                                           "Could not start BLE scan");
        Serial.printf("[RAD BLE][RADR] BLE scan started for %d ms\n", timeoutMs);
        return radble::Result::success();
    }

    if (operation == "indicator.set") {
        if (!args["brightness"].isNull() && !args["brightness"].is<int>())
            return radble::Result::failure("invalid_value",
                                           "Brightness must be an integer");
        const int brightness = args["brightness"] | 255;
        if (brightness < 0 || brightness > 255)
            return radble::Result::failure("invalid_value",
                                           "Brightness must be 0..255");
        if (path == "indicator.status") {
            if ((!args["hue"].isNull() && !args["hue"].is<int>()) ||
                (!args["durationMs"].isNull() &&
                 !args["durationMs"].is<int>()))
                return radble::Result::failure(
                    "invalid_value", "Hue and duration must be integers");
            const int hue = args["hue"] | 0;
            const int durationMs = args["durationMs"] | 250;
            if (hue < 0 || hue > 255 || durationMs < 0 || durationMs > 5000)
                return radble::Result::failure("invalid_value",
                                               "Indicator value is out of range");
            setLed(hue, brightness, durationMs);
        } else {
            if (!args["rgb565"].is<int>())
                return radble::Result::failure("invalid_value",
                                               "rgb565 must be an integer");
            const int requestedColor = args["rgb565"].as<int>();
            if (requestedColor < 0 || requestedColor > 65535)
                return radble::Result::failure("invalid_value",
                                               "rgb565 must be 0..65535");
            const uint16_t color = static_cast<uint16_t>(requestedColor);
            if (path == "indicator.left")
                setLeftEncoderLed(color, brightness);
            else if (path == "indicator.middle")
                setMiddleLed(color, brightness);
            else if (path == "indicator.right")
                setRightEncoderLed(color, brightness);
            else
                return radble::Result::failure("unknown_path",
                                               "Unknown indicator path");
        }
        Serial.printf("[RAD BLE][RADR] indicator %s brightness=%d\n",
                      path.c_str(), brightness);
        return radble::Result::success();
    }

    if (operation == "haptic.set" && path == "haptic.vibrator") {
        const String pattern = args["pattern"] | "single";
        if (pattern == "stop") stopVibrator();
        else if (pattern == "single")
            playVibratorPattern(VibratorPattern::SINGLE_PULSE);
        else if (pattern == "double")
            playVibratorPattern(VibratorPattern::DOUBLE_PULSE);
        else if (pattern == "triple")
            playVibratorPattern(VibratorPattern::TRIPLE_PULSE);
        else if (pattern == "error")
            playVibratorPattern(VibratorPattern::ERROR_PULSE);
        else return radble::Result::failure("invalid_value", "Unknown haptic pattern");
        Serial.printf("[RAD BLE][RADR] haptic %s\n", pattern.c_str());
        return radble::Result::success();
    }

    if (operation == "audio.set" && path == "audio.buzzer") {
        const String pattern = args["pattern"] | "single";
        if (pattern == "stop") stopBuzzer();
        else if (pattern == "single")
            playBuzzerPattern(BuzzerPattern::SINGLE_BEEP);
        else if (pattern == "double")
            playBuzzerPattern(BuzzerPattern::DOUBLE_BEEP);
        else if (pattern == "triple")
            playBuzzerPattern(BuzzerPattern::TRIPLE_BEEP);
        else if (pattern == "error")
            playBuzzerPattern(BuzzerPattern::ERROR_BEEP);
        else if (pattern == "radar")
            playBuzzerPattern(BuzzerPattern::RADAR_PING);
        else return radble::Result::failure("invalid_value", "Unknown audio pattern");
        Serial.printf("[RAD BLE][RADR] audio %s\n", pattern.c_str());
        return radble::Result::success();
    }

    if (operation == "display.set" && path == "display.backlight") {
        if (!args["value"].is<int>())
            return radble::Result::failure("invalid_value",
                                           "Backlight must be an integer");
        const int brightness = args["value"].as<int>();
        if (brightness < 0 || brightness > 255)
            return radble::Result::failure("invalid_value",
                                           "Backlight must be 0..255");
        setScreenBrightness(brightness);
        Serial.printf("[RAD BLE][RADR] backlight=%d\n", brightness);
        return radble::Result::success("{\"value\":" + String(brightness) + "}");
    }

    return radble::Result::failure("unsupported",
                                   "Operation is not supported");
}

String snapshot(radble::Surface surface, void*) {
    JsonDocument document;
    switch (surface) {
        case radble::Surface::State:
            document["state"] = stateName();
            document["connected"] = device != nullptr && device->isConnected;
            document["paused"] = device != nullptr && device->isPaused;
            document["idleState"] = static_cast<int>(idleState);
            document["memoryPresent"] = isMemoryChipFound;
            document["scanning"] = NimBLEDevice::getScan()->isScanning();
            break;
        case radble::Surface::Button:
            document["leftShoulder"] = digitalRead(pins::BTN_L_SHOULDER) == LOW;
            document["rightShoulder"] = digitalRead(pins::BTN_R_SHOULDER) == LOW;
            document["left"] = digitalRead(pins::BTN_UNDER_L) == LOW;
            document["middle"] = digitalRead(pins::BTN_UNDER_C) == LOW;
            document["right"] = digitalRead(pins::BTN_UNDER_R) == LOW;
            break;
        case radble::Surface::Encoder:
            document["left"] = leftEncoder.readEncoder();
            document["right"] = rightEncoder.readEncoder();
            document["leftChanged"] = hasLeftEncoderChanged(false);
            document["rightChanged"] = hasRightEncoderChanged(false);
            break;
        case radble::Surface::Imu: {
            updateIMUReadings();
            const IMUSnapshot imu = getIMUSnapshot();
            document["available"] = imu.available;
            document["x"] = imu.accelX;
            document["y"] = imu.accelY;
            document["z"] = imu.accelZ;
            document["gyroX"] = imu.gyroX;
            document["gyroY"] = imu.gyroY;
            document["gyroZ"] = imu.gyroZ;
            document["temperatureC"] = imu.temperatureC;
            break;
        }
        case radble::Surface::Power:
            document["percent"] = getBatteryPercent();
            document["voltage"] = getBatteryVoltage();
            document["charging"] = isCharging();
            document["chargeRate"] = lastChargeRate;
            break;
        case radble::Surface::Connectivity:
            document["wifi"] = WiFi.status() == WL_CONNECTED;
            document["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
            document["ip"] = WiFi.localIP().toString();
            document["scanning"] = NimBLEDevice::getScan()->isScanning();
            document["discovered"] = getDiscoveredDevices().size();
            document["deviceConnected"] = device != nullptr && device->isConnected;
            document["centralConnections"] =
                NimBLEDevice::getConnectedClients().size();
            document["peripheralConnections"] =
                NimBLEDevice::getServer() == nullptr
                    ? 0
                    : NimBLEDevice::getServer()->getConnectedCount();
            if (device != nullptr && device->advertisedDevice != nullptr) {
                document["peripheralName"] = device->getName();
                document["peripheralRssi"] =
                    device->advertisedDevice->getRSSI();
            }
            break;
        case radble::Surface::Indicator:
            document["available"] = true;
            document["count"] = pins::NUM_LEDS;
            {
            JsonArray values = document["leds"].to<JsonArray>();
            for (uint8_t index = 0; index < pins::NUM_LEDS; ++index) {
                JsonObject led = values.add<JsonObject>();
                led["r"] = leds[index].r;
                led["g"] = leds[index].g;
                led["b"] = leds[index].b;
            }
            }
            break;
        case radble::Surface::Haptic:
            document["available"] = true;
            break;
        case radble::Surface::Audio:
            document["available"] = true;
            break;
        case radble::Surface::Display:
            document["available"] = true;
            document["width"] = 320;
            document["height"] = 240;
            document["brightness"] = getScreenBrightness();
            break;
        default:
            return "{}";
    }
    String output;
    serializeJson(document, output);
    return output;
}

radble::Result prepareOta(bool starting, const char* component, void*) {
    if (!starting) {
        if (strcmp(component, "filesystem") == 0 && !LittleFS.begin())
            return radble::Result::failure(
                "storage_failed", "Could not remount the filesystem");
        return radble::Result::success();
    }
    if (device != nullptr && device->isConnected)
        return radble::Result::failure(
            "invalid_state", "Disconnect the controlled device before direct OTA");
    if (!isCharging() && getBatteryPercent() < 20)
        return radble::Result::failure(
            "preflight_failed", "Battery is too low for direct OTA");
    NimBLEDevice::getScan()->stop();
    stopVibrator();
    stopBuzzer();
    setScreenBrightness(BRIGHTNESS_FULL);
    if (strcmp(component, "filesystem") == 0) LittleFS.end();
    Serial.println("[RAD BLE][RADR] direct OTA safety state enabled");
    return radble::Result::success();
}

void releaseDiagnosticOutputs(void*) {
    stopVibrator();
    stopBuzzer();
    releaseAllIndividualLeds();
    setLed(LEDColors::idle, 50, 250);
    restoreScreenBrightness();
}

radble::Result prepareStream(const char* path, uint16_t rateHz, void*) {
    if (NimBLEDevice::getScan()->isScanning())
        return radble::Result::failure("busy", "Stop BLE scanning first");
    if (device != nullptr && device->isConnected && rateHz > 20)
        return radble::Result::failure(
            "busy", "High-rate streaming conflicts with peripheral control");
    if (strncmp(path, "imu.", 4) == 0 && !getIMUSnapshot().available)
        return radble::Result::failure("hardware_unavailable",
                                       "The IMU is not available");
    return radble::Result::success();
}

}  // namespace

radble::Server radBleServer;

bool initRadBle(NimBLEServer* server) {
    const bool imuAvailable = getIMUSnapshot().available;
    for (auto& resource : RESOURCES) {
        if (strncmp(resource.path, "imu.", 4) != 0) continue;
        if (imuAvailable)
            resource.flags |= radble::RESOURCE_AVAILABLE;
        else
            resource.flags &= ~radble::RESOURCE_AVAILABLE;
    }
    const radble::Config config = {
        .deviceType = "RADR",
        .deviceName = "RADR",
        .serviceUuid = radble::RADR_SERVICE_UUID,
        .firmwareVersion = VERSION,
        .build = FIRMWARE_BUILD_SHA,
        .capabilities = radble::CAP_BUTTON | radble::CAP_ENCODER |
                        (imuAvailable ? radble::CAP_IMU : 0) | radble::CAP_POWER |
                        radble::CAP_CONNECTIVITY | radble::CAP_INDICATOR |
                        radble::CAP_HAPTIC | radble::CAP_AUDIO |
                        radble::CAP_DISPLAY | radble::CAP_SENSOR_STREAM,
        .resources = RESOURCES,
        .resourceCount = sizeof(RESOURCES) / sizeof(RESOURCES[0]),
        .commandHandler = handleCommand,
        .snapshotHandler = snapshot,
        .otaDataHandler = nullptr,
        .context = nullptr,
        .directOta = true,
        .directFilesystemOta = true,
        .otaSafetyHandler = prepareOta,
        .leaseReleaseHandler = releaseDiagnosticOutputs,
        .createSurfaceCharacteristics = true,
        .streamSafetyHandler = prepareStream,
    };
    return radBleServer.begin(server, config);
}
