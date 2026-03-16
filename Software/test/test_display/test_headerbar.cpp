#include <cstdio>

#include "test_helpers.h"

struct WifiEntry {
    ui::WifiStatus status;
    const char *name;
};

struct BleEntry {
    ui::BleStatus status;
    const char *name;
};

struct BatteryEntry {
    ui::BatteryStatus status;
    const char *name;
};

static const WifiEntry wifiStates[] = {
    {ui::WifiStatus::DISCONNECTED, "wifi_disconnected"},
    {ui::WifiStatus::CONNECTED, "wifi_connected"},
    {ui::WifiStatus::ERROR, "wifi_error"},
};
static const int NUM_WIFI = sizeof(wifiStates) / sizeof(wifiStates[0]);

static const BleEntry bleStates[] = {
    {ui::BleStatus::OFF, "ble_off"},
    {ui::BleStatus::ON, "ble_on"},
    {ui::BleStatus::SCANNING, "ble_scanning"},
    {ui::BleStatus::CONNECTED, "ble_connected"},
};
static const int NUM_BLE = sizeof(bleStates) / sizeof(bleStates[0]);

static const BatteryEntry batteryStates[] = {
    {ui::BatteryStatus::EMPTY, "bat_empty"},
    {ui::BatteryStatus::LOW_BATTERY, "bat_low"},
    {ui::BatteryStatus::MID, "bat_mid"},
    {ui::BatteryStatus::FULL, "bat_full"},
    {ui::BatteryStatus::CHARGING, "bat_charging"},
};
static const int NUM_BATTERY =
    sizeof(batteryStates) / sizeof(batteryStates[0]);

// ============================================================
// All WiFi × BLE × Battery combinations
// ============================================================

void test_headerbar_allCombinations(void) {
    int combo = 0;
    for (int w = 0; w < NUM_WIFI; w++) {
        for (int b = 0; b < NUM_BLE; b++) {
            for (int bat = 0; bat < NUM_BATTERY; bat++) {
                canvas.fillScreen(0x0000);

                ui::HeaderBarData data{
                    wifiStates[w].status,
                    bleStates[b].status,
                    batteryStates[bat].status,
                };

                ui::drawHeaderBar(canvas, data);

                TEST_ASSERT_TRUE(bufferHasContent(canvas));

                char name[256];
                snprintf(name, sizeof(name), "%02d_%s_%s_%s", combo,
                         wifiStates[w].name, bleStates[b].name,
                         batteryStates[bat].name);
                TEST_ASSERT_TRUE(
                    savePPMGrouped(canvas, "headerbar", name));
                combo++;
            }
        }
    }
    printf("  -> Generated %d header bar combinations\n", combo);
}

// ============================================================
// Individual WiFi states
// ============================================================

void test_headerbar_wifi_disconnected(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.wifi = ui::WifiStatus::DISCONNECTED};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "headerbar/wifi", "disconnected"));
}

void test_headerbar_wifi_connected(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.wifi = ui::WifiStatus::CONNECTED};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "headerbar/wifi", "connected"));
}

void test_headerbar_wifi_error(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.wifi = ui::WifiStatus::ERROR};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "headerbar/wifi", "error"));
}

// ============================================================
// Individual BLE states
// ============================================================

void test_headerbar_ble_off(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.ble = ui::BleStatus::OFF};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "headerbar/ble", "off"));
}

void test_headerbar_ble_on(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.ble = ui::BleStatus::ON};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "headerbar/ble", "on"));
}

void test_headerbar_ble_scanning(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.ble = ui::BleStatus::SCANNING};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "headerbar/ble", "scanning"));
}

void test_headerbar_ble_connected(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.ble = ui::BleStatus::CONNECTED};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "headerbar/ble", "connected"));
}

// ============================================================
// Individual Battery states
// ============================================================

void test_headerbar_battery_empty(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.battery = ui::BatteryStatus::EMPTY};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "headerbar/battery", "empty"));
}

void test_headerbar_battery_low(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.battery = ui::BatteryStatus::LOW_BATTERY};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "headerbar/battery", "low"));
}

void test_headerbar_battery_mid(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.battery = ui::BatteryStatus::MID};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "headerbar/battery", "mid"));
}

void test_headerbar_battery_full(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.battery = ui::BatteryStatus::FULL};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "headerbar/battery", "full"));
}

void test_headerbar_battery_charging(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{.battery = ui::BatteryStatus::CHARGING};
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "headerbar/battery", "charging"));
}

// ============================================================
// Header bar draws only in statusbar region
// ============================================================

void test_headerbar_stays_in_statusbar_region(void) {
    canvas.fillScreen(0x0000);

    ui::HeaderBarData data{
        ui::WifiStatus::CONNECTED,
        ui::BleStatus::CONNECTED,
        ui::BatteryStatus::FULL,
    };
    ui::drawHeaderBar(canvas, data);

    uint16_t *buf = canvas.getBuffer();
    bool contentBelowStatusbar = false;
    for (int y = ui::Display::StatusbarHeight; y < ui::Display::HEIGHT;
         y++) {
        for (int x = 0; x < ui::Display::WIDTH; x++) {
            if (buf[y * ui::Display::WIDTH + x] != 0) {
                contentBelowStatusbar = true;
                break;
            }
        }
        if (contentBelowStatusbar) break;
    }
    TEST_ASSERT_FALSE(contentBelowStatusbar);
}

// ============================================================
// Header bar icons are centered
// ============================================================

void test_headerbar_icons_are_centered(void) {
    canvas.fillScreen(0x0000);

    ui::HeaderBarData data{
        ui::WifiStatus::CONNECTED,
        ui::BleStatus::CONNECTED,
        ui::BatteryStatus::FULL,
    };
    ui::drawHeaderBar(canvas, data);

    uint16_t *buf = canvas.getBuffer();

    int leftMost = ui::Display::WIDTH;
    int rightMost = 0;
    for (int y = 0; y < ui::Display::StatusbarHeight; y++) {
        for (int x = 0; x < ui::Display::WIDTH; x++) {
            if (buf[y * ui::Display::WIDTH + x] != 0) {
                if (x < leftMost) leftMost = x;
                if (x > rightMost) rightMost = x;
            }
        }
    }

    int center = (leftMost + rightMost) / 2;
    int displayCenter = ui::Display::WIDTH / 2;
    int tolerance = 15;
    TEST_ASSERT_INT_WITHIN(tolerance, displayCenter, center);
}

// ============================================================
// Default header bar data
// ============================================================

void test_headerbar_default_data(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data;
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "headerbar", "default"));
}

// ============================================================
// Worst case: all errors/empty
// ============================================================

void test_headerbar_worst_case(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{
        ui::WifiStatus::ERROR,
        ui::BleStatus::OFF,
        ui::BatteryStatus::EMPTY,
    };
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "headerbar", "worst_case"));
}

// ============================================================
// Best case: all good
// ============================================================

void test_headerbar_best_case(void) {
    canvas.fillScreen(0x0000);
    ui::HeaderBarData data{
        ui::WifiStatus::CONNECTED,
        ui::BleStatus::CONNECTED,
        ui::BatteryStatus::CHARGING,
    };
    ui::drawHeaderBar(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "headerbar", "best_case"));
}

// ============================================================
// Registration
// ============================================================

void register_headerbar_tests() {
    RUN_TEST(test_headerbar_wifi_disconnected);
    RUN_TEST(test_headerbar_wifi_connected);
    RUN_TEST(test_headerbar_wifi_error);

    RUN_TEST(test_headerbar_ble_off);
    RUN_TEST(test_headerbar_ble_on);
    RUN_TEST(test_headerbar_ble_scanning);
    RUN_TEST(test_headerbar_ble_connected);

    RUN_TEST(test_headerbar_battery_empty);
    RUN_TEST(test_headerbar_battery_low);
    RUN_TEST(test_headerbar_battery_mid);
    RUN_TEST(test_headerbar_battery_full);
    RUN_TEST(test_headerbar_battery_charging);

    RUN_TEST(test_headerbar_stays_in_statusbar_region);
    RUN_TEST(test_headerbar_icons_are_centered);
    RUN_TEST(test_headerbar_default_data);
    RUN_TEST(test_headerbar_worst_case);
    RUN_TEST(test_headerbar_best_case);

    RUN_TEST(test_headerbar_allCombinations);
}
