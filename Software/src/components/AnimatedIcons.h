#ifndef LOCKBOX_ANIMATEDICONS_H
#define LOCKBOX_ANIMATEDICONS_H

#include <Adafruit_GFX.h>
#include "utils/psramTask.h"
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>
#include <freertos/semphr.h>
#include <functional>
#include <services/battery.h>

#include "Arduino.h"
#include "WiFi.h"
#include "components/Icons.h"
#include "constants/Colors.h"
#include "constants/Sizes.h"
#include "esp_bt.h"
#include "pins.h"
#include "services/coms.h"
#include "services/display.h"
#include "services/encoder.h"
#include "services/lastInteraction.h"

class StateIcon {
  protected:
    int16_t x, y;
    int16_t width, height;
    uint16_t color;
    unsigned long lastDrawn = 0;
    // Track last drawn state to prevent unnecessary redraws
    const unsigned char *lastFrame = nullptr;
    uint16_t lastColor = 0;
    uint8_t lastStatus = 255;  // Initialize to invalid value

  public:
    bool showStatusDot = false;
    // 0 = off
    // 1 = yellow circle
    // 2 = yellow filled
    // 3 = green circle
    // 4 = green filled
    uint8_t status = 0;

    StateIcon(int16_t x, int16_t y)
        : x(x),
          y(y),
          width(Display::Icons::Small),
          height(Display::Icons::Small),
          color(Colors::white) {}

    void draw(Adafruit_ST7789 *display) {
        const unsigned char *currentFrame = getFrame();

        // Only redraw if something actually changed
        if (currentFrame == lastFrame && color == lastColor &&
            status == lastStatus) {
            return;
        }

        // Skip drawing if frame is null (used by some icons to skip frames)
        if (currentFrame == nullptr) {
            return;
        }

        if (xSemaphoreTake(displayMutex, 100) == pdTRUE) {
            // Clear the icon area
            display->fillRect(x, y, width, height, ST77XX_BLACK);

            // Draw the bitmap directly
            display->drawBitmap(x, y, currentFrame, width, height, color);

            if (showStatusDot) {
                drawStatusDot(display);
            }
            xSemaphoreGive(displayMutex);
        }

        // Update cached state
        lastFrame = currentFrame;
        lastColor = color;
        lastStatus = status;
        lastDrawn = millis();
    }

    void drawStatusDot(Adafruit_ST7789 *display) {
        uint size = 2;
        uint padding = 2;
        uint ypadding = 5;

        if (status != 0) {
            display->drawCircle(x + padding, y + height - ypadding, size + 1,
                                Colors::bgGray900);
            display->drawCircle(x + padding, y + height - ypadding, size + 2,
                                Colors::bgGray900);
        }

        if (status == 0) {
            // Cover up the MQTT circle
            display->fillCircle(x + padding, y + height - ypadding, size,
                                Colors::bgGray600);
        } else if (status == 1) {
            display->drawCircle(x + padding, y + height - ypadding, size,
                                Colors::yellow);
        } else if (status == 2) {
            display->fillCircle(x + padding, y + height - ypadding, size,
                                Colors::yellow);
        } else if (status == 3) {
            display->drawCircle(x + padding, y + height - ypadding, size,
                                Colors::green);
        } else if (status == 4) {
            display->fillCircle(x + padding, y + height - ypadding, size,
                                Colors::green);
        }
    }

    void forceRedraw() {
        // Reset cached state to force redraw on next update
        lastFrame = nullptr;
        lastColor = 0;
        lastStatus = 255;  // Invalid value to force redraw
        lastDrawn = 0;     // Force immediate redraw timing
    }

    virtual bool shouldDraw() { return millis() - lastDrawn > 1000; }

    virtual const unsigned char *getFrame() = 0;
};

class WifiStateIcon : public StateIcon {
  private:
    bool pingOn = false;
    wl_status_t lastWifiStatus = WL_IDLE_STATUS;

  public:
    WifiStateIcon(int16_t x, int16_t y) : StateIcon(x, y) {
        showStatusDot = true;
    }

    void forceStatusCheck() {
        // Reset status cache to force status recheck
        lastWifiStatus = WL_IDLE_STATUS;
    }

    const unsigned char *getFrame() override {
        wl_status_t currentStatus = WiFi.status();

        // Only update if WiFi state actually changed
        if (currentStatus == lastWifiStatus) {
            return lastFrame;  // Return cached frame to prevent redraw
        }

        lastWifiStatus = currentStatus;

        if (currentStatus == WL_CONNECTED) {
            this->status = 4;
            color = Colors::white;
            return bitmap_wifi;
        } else {
            this->status = 0;
            color = Colors::white;
            return bitmap_wifi_off;
        }
    }
};

class BatteryStateIcon : public StateIcon {
  private:
    int lastBatteryPercent = -1;
    bool lastChargingState = false;

  public:
    BatteryStateIcon(int16_t x, int16_t y) : StateIcon(x, y) {}

    void forceStatusCheck() {
        // Reset status cache to force status recheck
        lastBatteryPercent = -1;
        lastChargingState = false;
    }

    const unsigned char *getFrame() override {
        updateBatteryStatus();

        bool currentCharging = isCharging();
        int currentPercent = getBatteryPercent();

        // Only update if battery state actually changed
        if (currentCharging == lastChargingState &&
            currentPercent == lastBatteryPercent) {
            return lastFrame;  // Return cached frame to prevent redraw
        }

        lastChargingState = currentCharging;
        lastBatteryPercent = currentPercent;

        if (currentCharging) {
            color = Colors::green;
            return bitmap_battery_charging;
        }

        color = Colors::white;

        if (currentPercent >= 80) {
            return bitmap_battery_full;
        } else if (currentPercent >= 40) {
            return bitmap_battery_mid;
        } else if (currentPercent >= 20) {
            return bitmap_battery_low;
        } else {
            color = Colors::red;
            return bitmap_battery_empty;
        }
    }
};

class BLEStateIcon : public StateIcon {
  private:
    bool lastBleEnabled = false;
    bool lastScanning = false;
    int lastConnections = -1;

  public:
    BLEStateIcon(int16_t x, int16_t y) : StateIcon(x, y) {}

    void forceStatusCheck() {
        // Reset status cache to force status recheck
        lastBleEnabled = false;
        lastScanning = false;
        lastConnections = -1;
    }

    const unsigned char *getFrame() override {
        bool currentBleEnabled = (esp_bt_controller_get_status() ==
                                  ESP_BT_CONTROLLER_STATUS_ENABLED);
        bool currentScanning = NimBLEDevice::getScan()->isScanning();
        int currentConnections = NimBLEDevice::getConnectedClients().size();

        // Only update if BLE state actually changed
        if (currentBleEnabled == lastBleEnabled &&
            currentScanning == lastScanning &&
            currentConnections == lastConnections) {
            return lastFrame;  // Return cached frame to prevent redraw
        }

        lastBleEnabled = currentBleEnabled;
        lastScanning = currentScanning;
        lastConnections = currentConnections;

        if (!currentBleEnabled) {
            color = Colors::white;
            return researchAndDesireBluetoothOff;
        }

        // Nimble isScanning?
        if (currentScanning) {
            color = Colors::white;
            return researchAndDesireBluetoothConnect;
        }

        // how many connections do we have?
        if (currentConnections > 0) {
            color = Colors::green;
            return researchAndDesireBluetoothSignal;
        }

        color = Colors::white;
        return researchAndDesireBluetoothOn;
    }
};

// Global variables to access icons from other parts of the code
static WifiStateIcon *globalWifiIcon = nullptr;
static BatteryStateIcon *globalBatteryIcon = nullptr;
static BLEStateIcon *globalBleIcon = nullptr;

// Function to force all status icons to redraw and recheck status
static void forceStatusIconsRedraw() {
    // Reset the global icon index counter to ensure proper positioning
    extern int iconIdx;
    iconIdx = 0;

    if (globalWifiIcon) {
        globalWifiIcon->forceRedraw();
        globalWifiIcon->forceStatusCheck();
    }
    if (globalBatteryIcon) {
        globalBatteryIcon->forceRedraw();
        globalBatteryIcon->forceStatusCheck();
    }
    if (globalBleIcon) {
        globalBleIcon->forceRedraw();
        globalBleIcon->forceStatusCheck();
    }
}

// Example usage:
static void setupAnimatedIcons() {
    auto task = [](void *pvParameters) {
        BLEStateIcon bleIcon(getIconX(), 3);
        WifiStateIcon wifiIcon(getIconX(), 3);
        BatteryStateIcon batteryIcon(getIconX(), 3);

        // Set global pointers for external access
        globalBleIcon = &bleIcon;
        globalWifiIcon = &wifiIcon;
        globalBatteryIcon = &batteryIcon;

        while (true) {
            // If we're sleeping, don't draw anything in the status bar.
            if (idleState != IdleState::NOT_IDLE) {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }

            // Draw icons - they will only redraw if state actually changed
            wifiIcon.draw(&tft);
            batteryIcon.draw(&tft);
            bleIcon.draw(&tft);

            // Reduced frequency - check for updates every 250ms instead of
            // 100ms
            vTaskDelay(250 / portTICK_PERIOD_MS);
        }
    };

    createPsramTask(task, "draw_icons", 4 * configMINIMAL_STACK_SIZE, nullptr,
                    tskIDLE_PRIORITY, nullptr, 0);
}

#endif  // LOCKBOX_ANIMATEDICONS_H
