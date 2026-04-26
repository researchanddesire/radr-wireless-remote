#include "menus.h"

#include <components/DynamicText.h>
#include <devices/device.h>
#include <services/coms.h>
#include <services/encoder.h>
#include <state/remote.h>

#include "displayUtils.h"
#include "services/display.h"

TaskHandle_t menuTaskHandle = NULL;
static volatile bool menuTaskExitRequested = false;

std::vector<MenuItem> *activeMenu = &mainMenu;
int activeMenuCount = numMainMenu;
int currentOption = 0;

static const int scrollWidth = 6;

using namespace sml;

static const int menuWidth =
    Display::WIDTH - scrollWidth - Display::Padding::P1 * 2;
static const int menuItemHeight = Display::Icons::Small + Display::Padding::P2;
static const int menuItemDescriptionHeight = menuItemHeight * 1.5;

static int menuYOffset = 0;

static void drawMenuItem(int index, const MenuItem &option,
                         bool selected = false) {
    auto text = option.name;
    auto bitmap = option.bitmap;
    auto color = option.color > 0 ? option.color : Colors::textForeground;
    auto unfocusedColor = option.unfocusedColor > 0 ? option.unfocusedColor
                                                    : Colors::textBackground;

    int y = Display::StatusbarHeight + Display::Padding::P1 + menuYOffset;
    int x = Display::Padding::P1;

    bool shouldDrawDescription = option.description.has_value() && selected;

    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        // Clear menu item area
        tft.fillRect(x, y, menuWidth, menuItemHeight, Colors::black);

        if (index > 0) {
            tft.drawFastHLine(x + Display::Padding::P1, y,
                              menuWidth - Display::Padding::P2,
                              Colors::bgGray900);
        }

        if (selected) {
            tft.fillRoundRect(x, y, menuWidth,
                              shouldDrawDescription ? menuItemDescriptionHeight
                                                    : menuItemHeight,
                              3, Colors::bgGray900);
        }

        tft.setTextColor(selected ? color : unfocusedColor);
        tft.setFont(&FreeSans9pt7b);

        int padding = Display::Padding::P2;
        int textOffset = 6;

        tft.drawBitmap(x + padding, y + textOffset, bitmap,
                       Display::Icons::Small, Display::Icons::Small,
                       selected ? color : unfocusedColor);

        padding += Display::Icons::Small + Display::Padding::P2;
        tft.setCursor(x + padding, y + textOffset + menuItemHeight / 2);
        tft.print(text.c_str());

        if (shouldDrawDescription) {
            tft.setFont();
            tft.setTextColor(Colors::textForegroundSecondary);

            wrapText(tft, option.description.value().c_str(),
                     {.x = x + Display::Padding::P2,
                      .y = y + textOffset + Display::Icons::Small +
                           Display::Padding::P0,
                      .rightPadding = Display::Padding::P3});
        }

        menuYOffset +=
            shouldDrawDescription ? menuItemDescriptionHeight : menuItemHeight;

        xSemaphoreGive(displayMutex);
    }
}

void drawMenuFrame() {
    int numOptions = activeMenuCount;
    const MenuItem *options = activeMenu->data();

    // Since wrap-around is disabled, currentOption should always be within
    // bounds Just clamp it as a safety measure
    int safeCurrentOption = currentOption;
    if (safeCurrentOption < 0) safeCurrentOption = 0;
    if (safeCurrentOption >= numOptions) safeCurrentOption = numOptions - 1;

    menuYOffset = 0;

    menuYOffset = 0;

    if (numOptions <= 5) {
        for (int i = 0; i < numOptions; i++) {
            const MenuItem &option = options[i];
            bool isSelected = i == safeCurrentOption;
            drawMenuItem(i, option, isSelected);
        }
        return;
    }

    drawScrollBar(safeCurrentOption, numOptions - 1);

    for (int i = 0; i < 5; i++) {
        int optionIndex = safeCurrentOption - 2 + i;

        // Check if this position should show a menu item or be blank
        if (optionIndex < 0 || optionIndex >= numOptions) {
            // Draw actual blank area to clear any previous content
            int y =
                Display::StatusbarHeight + Display::Padding::P1 + menuYOffset;
            int x = Display::Padding::P1;

            if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                tft.fillRect(x, y, menuWidth, menuItemHeight, Colors::black);
                xSemaphoreGive(displayMutex);
            }
            menuYOffset += menuItemHeight;
            continue;
        }

        const MenuItem &option = options[optionIndex];
        bool isSelected = optionIndex == safeCurrentOption;
        drawMenuItem(i, option, isSelected);
    }
}

// Global string for dynamic text display - lives at file scope for persistence
static std::string encoderDisplayText = "";
static bool encoderDisplayNeedsCreation = true;

static void updateLeftEncoderValue(const std::string &label, int value) {
    // Update the persistent string that DynamicText monitors
    encoderDisplayText = label + ": " + std::to_string(value);
}

static void createEncoderDisplayObject() {
    if (device != nullptr && encoderDisplayNeedsCreation) {
        device->draw<DynamicText>(
            encoderDisplayText,    // reference to persistent string
            Display::Padding::P1,  // x position
            Display::StatusbarHeight - Display::Padding::P1  // y position
        );
        encoderDisplayNeedsCreation = false;
    }
}

void drawMenuTask(void *pvParameters) {
    int lastEncoderValue = -1;

    auto isInCorrectState = []() {
        return stateMachine->is("main_menu"_s) ||
               stateMachine->is("settings_menu"_s) ||
               stateMachine->is("device_menu"_s) ||
               stateMachine->is("device_settings_menu"_s);
    };

    auto isInNestedState = []() { return stateMachine->is("device_menu"_s); };

    // Ensure global handle is set for lifecycle coordination
    menuTaskHandle = xTaskGetCurrentTaskHandle();

    // Mark encoder display as needing creation for device menu
    if (device != nullptr && stateMachine->is("device_menu"_s) &&
        device->needsPersistentLeftEncoderMonitoring()) {
        encoderDisplayNeedsCreation = true;
    }

    // Important!!
    // Never run setEncoderValue without ensuring you're in the expected
    // state. Not doing this can cause the device_controller to receive the
    // update, causing unexpected setting to be sent to the device.
    bool initialized = false;
    while (!initialized) {
        if (isInCorrectState()) {
            // Disable wrap-around (false) to eliminate the problematic behavior
            rightEncoder.setBoundaries(0, activeMenuCount - 1, false);
            rightEncoder.setAcceleration(0);
            // Ensure currentOption is within bounds for the new menu
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

        // Check if we need to update left encoder persistent display for
        // devices with persistent encoder monitoring
        static int lastLeftEncoderValue = -1;
        static bool isFirstDeviceMenuEntry = true;
        bool shouldUpdateLeftEncoderValue = false;
        int currentLeftEncoderValue = 0;

        if (isInNestedState() && device != nullptr &&
            device->needsPersistentLeftEncoderMonitoring()) {
            currentLeftEncoderValue = device->getCurrentLeftEncoderValue();

            // Create display object on first entry
            createEncoderDisplayObject();

            // Force display on first entry or when left encoder value changes
            if (isFirstDeviceMenuEntry ||
                currentLeftEncoderValue != lastLeftEncoderValue) {
                lastLeftEncoderValue = currentLeftEncoderValue;
                shouldUpdateLeftEncoderValue = true;
                isFirstDeviceMenuEntry = false;
            }
        } else {
            // Reset flag when not in device menu
            isFirstDeviceMenuEntry = true;
        }

        if (lastEncoderValue == currentOption &&
            !shouldUpdateLeftEncoderValue) {
            // No changes needed, just tick display objects
        } else {
            if (lastEncoderValue != currentOption) {
                lastEncoderValue = currentOption;
                drawMenuFrame();
            }

            if (shouldUpdateLeftEncoderValue) {
                updateLeftEncoderValue(device->getLeftEncoderParameterName(),
                                       currentLeftEncoderValue);
            }
        }

        // Tick all display objects
        if (device != nullptr) {
            for (auto &displayObject : device->displayObjects) {
                displayObject->tick();
            }
        }

        vTaskDelay(16 / portTICK_PERIOD_MS);
    }

    // Mark as finished before self-delete so creator can proceed safely
    menuTaskHandle = NULL;
    vTaskDelete(NULL);
}

void drawMenu() {
    // If an existing task is running, request cooperative exit and wait
    if (menuTaskHandle != NULL) {
        menuTaskExitRequested = true;
        // Reduced timeout for faster transitions
        const TickType_t waitStart = xTaskGetTickCount();
        const TickType_t waitTimeout =
            pdMS_TO_TICKS(50);  // Reduced from 200ms to 50ms
        while (menuTaskHandle != NULL &&
               (xTaskGetTickCount() - waitStart) < waitTimeout) {
            vTaskDelay(1);
        }
        // If still not null after timeout, force cleanup
        if (menuTaskHandle != NULL) {
            vTaskSuspend(menuTaskHandle);
            vTaskDelete(menuTaskHandle);
            menuTaskHandle = NULL;
        }
    }

    // Reset exit flag before creating a new task
    menuTaskExitRequested = false;

    ESP_LOGD("MENU", "Drawing menu");

    clearPage();
    // Reduced delay for faster startup
    vTaskDelay(10 / portTICK_PERIOD_MS);  // Reduced from 50ms to 10ms
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

    auto &devices = getDiscoveredDevices();

    if (devices.empty()) {
        // Add a "No devices found" placeholder
        deviceListMenu.push_back({MenuItemE::DEVICE_MENU_ITEM,
                                  "No devices found", bitmap_ble_connect,
                                  std::nullopt, Colors::textForegroundSecondary,
                                  Colors::textForegroundSecondary, -1});
    } else {
        for (size_t i = 0; i < devices.size(); i++) {
            std::string displayName = devices[i].name;
            if (displayName.empty()) {
                displayName = "Unknown Device";
            }

            // Add RSSI indicator
            // displayName += " (" + std::to_string(devices[i].rssi) + " dBm)";

            deviceListMenu.push_back(
                {MenuItemE::DEVICE_MENU_ITEM, displayName, bitmap_ble_connect,
                 std::nullopt, Colors::textForeground, Colors::textBackground,
                 static_cast<int>(i)});
        }
    }

    deviceListCount = deviceListMenu.size();
}

void drawDeviceListTask(void *pvParameters) {
    int lastEncoderValue = -1;

    auto isInCorrectState = []() { return stateMachine->is("device_list"_s); };

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

            // Redraw menu with updated selection
            activeMenu = &deviceListMenu;
            activeMenuCount = deviceListCount;
            drawMenuFrame();
        }

        vTaskDelay(16 / portTICK_PERIOD_MS);
    }

    deviceListTaskHandle = NULL;
    vTaskDelete(NULL);
}

void drawDeviceListMenu() {
    // Stop any existing device list task
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

    // Build the menu from discovered devices
    buildDeviceListMenu();

    // Set active menu
    activeMenu = &deviceListMenu;
    activeMenuCount = deviceListCount;
    currentOption = 0;

    clearPage();
    vTaskDelay(10 / portTICK_PERIOD_MS);

    xTaskCreatePinnedToCore(drawDeviceListTask, "drawDeviceListTask",
                            5 * configMINIMAL_STACK_SIZE, NULL, 5,
                            &deviceListTaskHandle, 1);
}