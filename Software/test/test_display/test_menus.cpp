#include "test_helpers.h"

// ============================================================
// Main menu
// ============================================================

static ui::MenuItem mainMenuItems[] = {
    {ui::DEVICE_SEARCH, ui::strings::OSSM_CONTROLLER_NAME,
     ui::icons::researchAndDesireWaves},
    {ui::SETTINGS, ui::strings::SETTINGS_NAME, ui::icons::bitmap_settings},
    {ui::DEEP_SLEEP, ui::strings::DEEP_SLEEP_NAME, ui::icons::bitmap_sleep},
};
static const int mainMenuCount = 3;

void test_menu_main_first(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {mainMenuItems, mainMenuCount, 0};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "menus", "main_first"));
}

void test_menu_main_second(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {mainMenuItems, mainMenuCount, 1};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "menus", "main_second"));
}

void test_menu_main_third(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {mainMenuItems, mainMenuCount, 2};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "menus", "main_third"));
}

// ============================================================
// Settings menu
// ============================================================

static ui::MenuItem settingsMenuItems[] = {
    {ui::BACK, ui::strings::GO_BACK_NAME, ui::icons::bitmap_back},
    {ui::WIFI_SETTINGS, ui::strings::WIFI_SETTINGS_NAME,
     ui::icons::bitmap_wifi},
    {ui::UPDATE, ui::strings::UPDATE_NAME, ui::icons::bitmap_update},
    {ui::RESTART, ui::strings::RESTART_NAME, ui::icons::bitmap_restart},
};
static const int settingsMenuCount = 4;

void test_menu_settings_first(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {settingsMenuItems, settingsMenuCount, 0};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "menus", "settings_first"));
}

void test_menu_settings_third(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {settingsMenuItems, settingsMenuCount, 2};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "menus", "settings_third"));
}

// ============================================================
// Long menu (>5 items, triggers scrollbar)
// ============================================================

static ui::MenuItem longMenuItems[] = {
    {ui::DEVICE_MENU_ITEM, "Device Alpha", ui::icons::bitmap_ble_connect},
    {ui::DEVICE_MENU_ITEM, "Device Beta", ui::icons::bitmap_ble_connect},
    {ui::DEVICE_MENU_ITEM, "Device Gamma", ui::icons::bitmap_ble_connect},
    {ui::DEVICE_MENU_ITEM, "Device Delta", ui::icons::bitmap_ble_connect},
    {ui::DEVICE_MENU_ITEM, "Device Epsilon", ui::icons::bitmap_ble_connect},
    {ui::DEVICE_MENU_ITEM, "Device Zeta", ui::icons::bitmap_ble_connect},
    {ui::DEVICE_MENU_ITEM, "Device Eta", ui::icons::bitmap_ble_connect},
    {ui::DEVICE_MENU_ITEM, "Device Theta", ui::icons::bitmap_ble_connect},
};
static const int longMenuCount = 8;

void test_menu_long_top(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {longMenuItems, longMenuCount, 0};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "menus", "long_top"));
}

void test_menu_long_middle(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {longMenuItems, longMenuCount, 4};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "menus", "long_middle"));
}

void test_menu_long_bottom(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {longMenuItems, longMenuCount, 7};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "menus", "long_bottom"));
}

// ============================================================
// Menu with descriptions
// ============================================================

static ui::MenuItem descMenuItems[] = {
    {ui::DEVICE_SEARCH, "Scan BLE", ui::icons::bitmap_ble_connect,
     "Search for nearby Bluetooth devices to connect."},
    {ui::SETTINGS, "Settings", ui::icons::bitmap_settings,
     "Configure WiFi, updates, and device options."},
    {ui::DEEP_SLEEP, "Sleep", ui::icons::bitmap_sleep,
     "Enter deep sleep mode to conserve battery."},
};
static const int descMenuCount = 3;

void test_menu_with_descriptions(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {descMenuItems, descMenuCount, 0};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "menus", "with_descriptions"));
}

// ============================================================
// Registration
// ============================================================

void register_menu_tests() {
    RUN_TEST(test_menu_main_first);
    RUN_TEST(test_menu_main_second);
    RUN_TEST(test_menu_main_third);
    RUN_TEST(test_menu_settings_first);
    RUN_TEST(test_menu_settings_third);
    RUN_TEST(test_menu_long_top);
    RUN_TEST(test_menu_long_middle);
    RUN_TEST(test_menu_long_bottom);
    RUN_TEST(test_menu_with_descriptions);
}
