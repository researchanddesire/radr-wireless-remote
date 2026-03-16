#include "menus.h"

#include <components/DynamicText.h>
#include <devices/device.h>
#include <services/encoder.h>
#include <services/coms.h>
#include <state/remote.h>
#include <ui.h>

#include "displayUtils.h"
#include "services/display.h"

TaskHandle_t menuTaskHandle = NULL;
static volatile bool menuTaskExitRequested = false;

std::vector<MenuItem> *activeMenu = &mainMenu;
int activeMenuCount = numMainMenu;
int currentOption = 0;

using namespace sml;

static void drawMenuFrameViaLib() {
    int numOptions = activeMenuCount;
    const MenuItem *options = activeMenu->data();

    int safeCurrentOption = currentOption;
    if (safeCurrentOption < 0) safeCurrentOption = 0;
    if (safeCurrentOption >= numOptions) safeCurrentOption = numOptions - 1;

    // Convert firmware MenuItems to ui::MenuItems
    std::vector<ui::MenuItem> uiItems(numOptions);
    for (int i = 0; i < numOptions; i++) {
        uiItems[i].id = static_cast<ui::MenuItemE>(options[i].id);
        uiItems[i].name = options[i].name;
        uiItems[i].bitmap = options[i].bitmap;
        if (options[i].description.has_value()) {
            uiItems[i].description = options[i].description.value();
        }
        uiItems[i].color = options[i].color;
        uiItems[i].unfocusedColor = options[i].unfocusedColor;
        uiItems[i].metaIndex = options[i].metaIndex;
    }

    ui::MenuData data = {uiItems.data(), numOptions, safeCurrentOption};

    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        ui::drawMenu(tft, data);
        xSemaphoreGive(displayMutex);
    }
}

// Global string for dynamic text display
static std::string encoderDisplayText = "";
static bool encoderDisplayNeedsCreation = true;

static void updateLeftEncoderValue(const std::string &label, int value) {
    encoderDisplayText = label + ": " + std::to_string(value);
}

static void createEncoderDisplayObject() {
    if (device != nullptr && encoderDisplayNeedsCreation) {
        device->draw<DynamicText>(
            encoderDisplayText,
            Display::Padding::P1,
            Display::StatusbarHeight - Display::Padding::P1);
        encoderDisplayNeedsCreation = false;
    }
}

void drawMenuTask(void *pvParameters) {
    int lastEncoderValue = -1;

    auto isInCorrectState = []() {
        return stateMachine->is("main_menu"_s) ||
               stateMachine->is("settings_menu"_s) ||
               stateMachine->is("device_menu"_s);
    };

    auto isInNestedState = []() { return stateMachine->is("device_menu"_s); };

    menuTaskHandle = xTaskGetCurrentTaskHandle();

    if (device != nullptr && stateMachine->is("device_menu"_s) &&
        device->needsPersistentLeftEncoderMonitoring()) {
        encoderDisplayNeedsCreation = true;
    }

    bool initialized = false;
    while (!initialized) {
        if (isInCorrectState()) {
            rightEncoder.setBoundaries(0, activeMenuCount - 1, false);
            rightEncoder.setAcceleration(0);
            int boundedCurrentOption = currentOption % activeMenuCount;
            if (boundedCurrentOption < 0) {
                boundedCurrentOption += activeMenuCount;
            }
            rightEncoder.setEncoderValue(boundedCurrentOption);
            currentOption = boundedCurrentOption;

            initialized = true;
        }
        vTaskDelay(1);
    }

    while (isInCorrectState() && !menuTaskExitRequested) {
        int rawEncoderValue = rightEncoder.readEncoder();
        currentOption = rawEncoderValue;

        static int lastLeftEncoderValue = -1;
        static bool isFirstDeviceMenuEntry = true;
        bool shouldUpdateLeftEncoderValue = false;
        int currentLeftEncoderValue = 0;

        if (isInNestedState() && device != nullptr &&
            device->needsPersistentLeftEncoderMonitoring()) {
            currentLeftEncoderValue = device->getCurrentLeftEncoderValue();

            createEncoderDisplayObject();

            if (isFirstDeviceMenuEntry ||
                currentLeftEncoderValue != lastLeftEncoderValue) {
                lastLeftEncoderValue = currentLeftEncoderValue;
                shouldUpdateLeftEncoderValue = true;
                isFirstDeviceMenuEntry = false;
            }
        } else {
            isFirstDeviceMenuEntry = true;
        }

        if (lastEncoderValue == currentOption &&
            !shouldUpdateLeftEncoderValue) {
            // No changes needed
        } else {
            if (lastEncoderValue != currentOption) {
                lastEncoderValue = currentOption;
                drawMenuFrameViaLib();
            }

            if (shouldUpdateLeftEncoderValue) {
                updateLeftEncoderValue(device->getLeftEncoderParameterName(),
                                       currentLeftEncoderValue);
            }
        }

        if (device != nullptr) {
            for (auto &displayObject : device->displayObjects) {
                displayObject->tick();
            }
        }

        vTaskDelay(16 / portTICK_PERIOD_MS);
    }

    menuTaskHandle = NULL;
    vTaskDelete(NULL);
}

void drawMenu() {
    if (menuTaskHandle != NULL) {
        menuTaskExitRequested = true;
        const TickType_t waitStart = xTaskGetTickCount();
        const TickType_t waitTimeout = pdMS_TO_TICKS(50);
        while (menuTaskHandle != NULL &&
               (xTaskGetTickCount() - waitStart) < waitTimeout) {
            vTaskDelay(1);
        }
        if (menuTaskHandle != NULL) {
            vTaskSuspend(menuTaskHandle);
            vTaskDelete(menuTaskHandle);
            menuTaskHandle = NULL;
        }
    }

    menuTaskExitRequested = false;

    ESP_LOGD("MENU", "Drawing menu");

    clearPage();
    vTaskDelay(10 / portTICK_PERIOD_MS);
    xTaskCreatePinnedToCore(drawMenuTask, "drawMenuTask",
                            5 * configMINIMAL_STACK_SIZE, NULL, 5,
                            &menuTaskHandle, 1);
}

// Device list management
static std::vector<MenuItem> deviceListMenu;
static int deviceListCount = 0;
TaskHandle_t deviceListTaskHandle = NULL;
static volatile bool deviceListTaskExitRequested = false;

void buildDeviceListMenu() {
    deviceListMenu.clear();

    auto& devices = getDiscoveredDevices();

    if (devices.empty()) {
        deviceListMenu.push_back({
            MenuItemE::DEVICE_MENU_ITEM,
            ui::strings::NO_DEVICES_FOUND,
            bitmap_ble_connect,
            std::nullopt,
            Colors::textForegroundSecondary,
            Colors::textForegroundSecondary,
            -1
        });
    } else {
        for (size_t i = 0; i < devices.size(); i++) {
            std::string displayName = devices[i].name;
            if (displayName.empty()) {
                displayName = ui::strings::UNKNOWN_DEVICE;
            }

            deviceListMenu.push_back({
                MenuItemE::DEVICE_MENU_ITEM,
                displayName,
                bitmap_ble_connect,
                std::nullopt,
                Colors::textForeground,
                Colors::textBackground,
                static_cast<int>(i)
            });
        }
    }

    deviceListCount = deviceListMenu.size();
}

void drawDeviceListTask(void *pvParameters) {
    int lastEncoderValue = -1;

    auto isInCorrectState = []() {
        return stateMachine->is("device_list"_s);
    };

    deviceListTaskHandle = xTaskGetCurrentTaskHandle();

    bool initialized = false;
    while (!initialized) {
        if (isInCorrectState()) {
            rightEncoder.setBoundaries(0, deviceListCount - 1, false);
            rightEncoder.setAcceleration(0);
            rightEncoder.setEncoderValue(0);
            currentOption = 0;
            initialized = true;
        }
        vTaskDelay(1);
    }

    while (isInCorrectState() && !deviceListTaskExitRequested) {
        int rawEncoderValue = rightEncoder.readEncoder();
        currentOption = rawEncoderValue;

        if (lastEncoderValue != currentOption) {
            lastEncoderValue = currentOption;

            activeMenu = &deviceListMenu;
            activeMenuCount = deviceListCount;
            drawMenuFrameViaLib();
        }

        vTaskDelay(16 / portTICK_PERIOD_MS);
    }

    deviceListTaskHandle = NULL;
    vTaskDelete(NULL);
}

void drawDeviceListMenu() {
    if (deviceListTaskHandle != NULL) {
        deviceListTaskExitRequested = true;
        const TickType_t waitStart = xTaskGetTickCount();
        const TickType_t waitTimeout = pdMS_TO_TICKS(50);
        while (deviceListTaskHandle != NULL &&
               (xTaskGetTickCount() - waitStart) < waitTimeout) {
            vTaskDelay(1);
        }
        if (deviceListTaskHandle != NULL) {
            vTaskSuspend(deviceListTaskHandle);
            vTaskDelete(deviceListTaskHandle);
            deviceListTaskHandle = NULL;
        }
    }

    deviceListTaskExitRequested = false;

    ESP_LOGD("DEVICE_LIST", "Drawing device list");

    buildDeviceListMenu();

    activeMenu = &deviceListMenu;
    activeMenuCount = deviceListCount;
    currentOption = 0;

    clearPage();
    vTaskDelay(10 / portTICK_PERIOD_MS);

    xTaskCreatePinnedToCore(drawDeviceListTask, "drawDeviceListTask",
                            5 * configMINIMAL_STACK_SIZE, NULL, 5,
                            &deviceListTaskHandle, 1);
}
