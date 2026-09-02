#include "menus.h"
#include "utils/psramTask.h"

#include <Fonts/FreeSans9pt7b.h>
#include <components/DynamicText.h>
#include <components/TextButton.h>
#include <devices/device.h>
#include <pins.h>
#include <services/encoder.h>
#include <services/coms.h>
#include <state/remote.h>

#include "displayUtils.h"
#include "controller.h"
#include "services/display.h"

TaskHandle_t menuTaskHandle = NULL;
static volatile bool menuTaskExitRequested = false;

std::vector<MenuItem> *activeMenu = &mainMenu;
int activeMenuCount = numMainMenu;
int currentOption = 0;
int activeTab = 0;
int tabBarHeight = 0;

static const int scrollWidth = 6;

using namespace sml;

static const int menuWidth =
    Display::WIDTH - scrollWidth - Display::Padding::P1 * 2;
static const int menuItemHeight = Display::Icons::Small + Display::Padding::P2;
static const int menuItemDescriptionHeight = menuItemHeight * 1.5;

static int menuYOffset = 0;

static const int tabHeight = 24;
static const int tabGap = 4;  // gap below tab bar

void drawTabBar() {
    const int16_t tabY = Display::StatusbarHeight;
    const int16_t tabWidth = Display::WIDTH / 2;

    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        // Clear tab bar area
        tft.fillRect(0, tabY, Display::WIDTH, tabHeight, Colors::black);

        // OSSM tab (left)
        bool ossmActive = (activeTab == 0);
        tft.fillRect(0, tabY, tabWidth, tabHeight,
                     ossmActive ? Colors::bgGray900 : Colors::black);
        tft.setFont(&FreeSans9pt7b);
        tft.setTextColor(ossmActive ? Colors::textForeground
                                    : Colors::disabled);
        // Center "OSSM" text in left half
        int16_t x1, y1;
        uint16_t tw, th;
        tft.getTextBounds("OSSM", 0, 0, &x1, &y1, &tw, &th);
        tft.setCursor((tabWidth - tw) / 2, tabY + tabHeight / 2 + th / 2);
        tft.print("OSSM");

        // RADR tab (right)
        bool radrActive = (activeTab == 1);
        tft.fillRect(tabWidth, tabY, tabWidth, tabHeight,
                     radrActive ? Colors::bgGray900 : Colors::black);
        tft.setTextColor(radrActive ? Colors::textForeground
                                    : Colors::disabled);
        tft.getTextBounds("RADR", 0, 0, &x1, &y1, &tw, &th);
        tft.setCursor(tabWidth + (tabWidth - tw) / 2,
                      tabY + tabHeight / 2 + th / 2);
        tft.print("RADR");

        xSemaphoreGive(displayMutex);
    }
}

void drawMenuWithTabs() {
    tabBarHeight = tabHeight + tabGap;

    // If OSSM tab is active but no device connected, show placeholder
    if (activeTab == 0 && (device == nullptr || !device->isConnected)) {
        activeMenu = nullptr;
        activeMenuCount = 0;
        drawTabBar();

        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            int placeholderY =
                Display::StatusbarHeight + tabBarHeight + Display::PageHeight / 3;
            tft.setFont(&FreeSans9pt7b);
            tft.setTextColor(Colors::disabled);
            int16_t x1, y1;
            uint16_t tw, th;
            tft.getTextBounds("No OSSM connected", 0, 0, &x1, &y1, &tw, &th);
            tft.setCursor((Display::WIDTH - tw) / 2, placeholderY);
            tft.print("No OSSM connected");
            xSemaphoreGive(displayMutex);
        }
        return;
    }

    drawMenu();
}

static void drawMenuItem(int index, const MenuItem &option,
                         bool selected = false) {
    auto text = option.name;
    auto bitmap = option.bitmap;
    auto color = option.color > 0 ? option.color : Colors::textForeground;
    auto unfocusedColor = option.unfocusedColor > 0 ? option.unfocusedColor
                                                    : Colors::textBackground;

    int y = Display::StatusbarHeight + Display::Padding::P1 + menuYOffset + tabBarHeight;
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

    // Since wrap-around is disabled, currentOption should always be within bounds
    // Just clamp it as a safety measure
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

    // Clamp start index so items pin to top/bottom edges (no blank gaps)
    int startIndex = safeCurrentOption - 2;
    if (startIndex < 0) startIndex = 0;
    if (startIndex > numOptions - 5) startIndex = numOptions - 5;

    for (int i = 0; i < 5; i++) {
        int optionIndex = startIndex + i;
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
               stateMachine->is("device_menu"_s);
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
        if (isInCorrectState() && activeMenuCount > 0) {
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

    // Draw tab bar after initialization (after clearPage in drawMenu)
    if (tabBarHeight > 0) {
        drawTabBar();
    }

    // Shoulder button state for tab switching (only in main_menu)
    bool lastLeftShoulderState = HIGH;
    bool lastRightShoulderState = HIGH;

    auto isInMainMenu = []() {
        return stateMachine->is("main_menu"_s);
    };

    while (isInCorrectState() && !menuTaskExitRequested) {
        // Shoulder button polling for tab switching (main_menu only)
        if (isInMainMenu() && tabBarHeight > 0) {
            bool curLeftShoulder = digitalRead(pins::BTN_L_SHOULDER);
            bool curRightShoulder = digitalRead(pins::BTN_R_SHOULDER);

            bool shouldToggle =
                (curLeftShoulder == LOW && lastLeftShoulderState == HIGH) ||
                (curRightShoulder == LOW && lastRightShoulderState == HIGH);

            if (shouldToggle) {
                activeTab = (activeTab == 0) ? 1 : 0;
                if (activeTab == 0) {
                    if (device != nullptr && device->isConnected) {
                        activeMenu = &ossmMenu;
                        activeMenuCount = numOssmMenu;
                    } else {
                        activeMenu = nullptr;
                        activeMenuCount = 0;
                    }
                } else {
                    activeMenu = &mainMenu;
                    activeMenuCount = numMainMenu;
                }
                currentOption = 0;

                clearPage();
                drawTabBar();

                if (activeMenu != nullptr && activeMenuCount > 0) {
                    rightEncoder.setBoundaries(0, activeMenuCount - 1, false);
                    rightEncoder.setEncoderValue(0);
                    lastEncoderValue = -1;  // Force redraw
                    drawMenuFrame();
                } else {
                    // No OSSM connected placeholder
                    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) ==
                        pdTRUE) {
                        int placeholderY =
                            Display::StatusbarHeight + tabBarHeight +
                            Display::PageHeight / 3;
                        tft.setFont(&FreeSans9pt7b);
                        tft.setTextColor(Colors::disabled);
                        int16_t x1, y1;
                        uint16_t tw, th;
                        tft.getTextBounds("No OSSM connected", 0, 0, &x1, &y1,
                                          &tw, &th);
                        tft.setCursor((Display::WIDTH - tw) / 2, placeholderY);
                        tft.print("No OSSM connected");
                        xSemaphoreGive(displayMutex);
                    }
                }
            }

            lastLeftShoulderState = curLeftShoulder;
            lastRightShoulderState = curRightShoulder;
        }

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
    exitPsramTask();
}

void stopMenuTask() {
    // If an existing task is running, request cooperative exit and wait
    if (menuTaskHandle != NULL) {
        menuTaskExitRequested = true;
        const TickType_t waitStart = xTaskGetTickCount();
        const TickType_t waitTimeout = pdMS_TO_TICKS(100);
        while (menuTaskHandle != NULL &&
               (xTaskGetTickCount() - waitStart) < waitTimeout) {
            vTaskDelay(1);
        }
        // If still not null after timeout, force cleanup
        if (menuTaskHandle != NULL) {
            TaskHandle_t stuck = menuTaskHandle;
            menuTaskHandle = NULL;
            vTaskSuspend(stuck);
            deletePsramTask(stuck);
        }
    }
    menuTaskExitRequested = false;
}

void drawMenu() {
    stopMenuTask();
    // The controller task and the menu task both tick device->displayObjects;
    // make sure only one of them is alive.
    stopControllerTask();

    ESP_LOGD("MENU", "Drawing menu");

    clearPage();
    // Reduced delay for faster startup
    vTaskDelay(10 / portTICK_PERIOD_MS);  // Reduced from 50ms to 10ms
    createPsramTask(drawMenuTask, "drawMenuTask", 5 * configMINIMAL_STACK_SIZE,
                    NULL, 5, &menuTaskHandle, 1);
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
        // Add a "No devices found" placeholder
        deviceListMenu.push_back({
            MenuItemE::DEVICE_MENU_ITEM,
            "No devices found",
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
                displayName = "Unknown Device";
            }
            
            // Add RSSI indicator
            // displayName += " (" + std::to_string(devices[i].rssi) + " dBm)";
            
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
            
            // Redraw menu with updated selection
            activeMenu = &deviceListMenu;
            activeMenuCount = deviceListCount;
            drawMenuFrame();
        }
        
        vTaskDelay(16 / portTICK_PERIOD_MS);
    }
    
    deviceListTaskHandle = NULL;
    exitPsramTask();
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
            deletePsramTask(deviceListTaskHandle);
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

    // Draw button labels
    const int16_t buttonY = Display::HEIGHT - 30;
    TextButton selectButton(SELECT_STRING, pins::BTN_UNDER_R, Display::WIDTH - 90, buttonY);
    selectButton.tick();
    TextButton retryButton(RETRY_STRING, pins::BTN_UNDER_C, (Display::WIDTH - 70) / 2, buttonY);
    retryButton.tick();

    createPsramTask(drawDeviceListTask, "drawDeviceListTask",
                    5 * configMINIMAL_STACK_SIZE, NULL, 5, &deviceListTaskHandle,
                    1);
}