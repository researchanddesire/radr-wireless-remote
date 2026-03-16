#include "test_helpers.h"

// ============================================================
// OSSM Pattern menu — all 7 OSSM stroke patterns
// Uses the same icons mapped by ossm_device.hpp::onConnect
// ============================================================

static ui::MenuItem ossmPatternItems[] = {
    {ui::DEVICE_MENU_ITEM, "Simple Stroke",
     ui::icons::researchAndDesireWaves},
    {ui::DEVICE_MENU_ITEM, "Teasing Pounding",
     ui::icons::researchAndDesireWaves},
    {ui::DEVICE_MENU_ITEM, "Robo Stroke",
     ui::icons::researchAndDesireWaves},
    {ui::DEVICE_MENU_ITEM, "Half n Half",
     ui::icons::researchAndDesireWaves},
    {ui::DEVICE_MENU_ITEM, "Deeper",
     ui::icons::researchAndDesireWaves},
    {ui::DEVICE_MENU_ITEM, "Stop n Go",
     ui::icons::researchAndDesireWaves},
    {ui::DEVICE_MENU_ITEM, "Insist",
     ui::icons::researchAndDesireWaves},
};
static const int ossmPatternCount = 7;

// Individual pattern selection tests — each pattern focused

void test_ossm_pattern_simple_stroke(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {ossmPatternItems, ossmPatternCount, 0};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "pattern_simple_stroke"));
}

void test_ossm_pattern_teasing_pounding(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {ossmPatternItems, ossmPatternCount, 1};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "pattern_teasing_pounding"));
}

void test_ossm_pattern_robo_stroke(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {ossmPatternItems, ossmPatternCount, 2};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "pattern_robo_stroke"));
}

void test_ossm_pattern_half_n_half(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {ossmPatternItems, ossmPatternCount, 3};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "pattern_half_n_half"));
}

void test_ossm_pattern_deeper(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {ossmPatternItems, ossmPatternCount, 4};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "pattern_deeper"));
}

void test_ossm_pattern_stop_n_go(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {ossmPatternItems, ossmPatternCount, 5};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "pattern_stop_n_go"));
}

void test_ossm_pattern_insist(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {ossmPatternItems, ossmPatternCount, 6};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "ossm/menus", "pattern_insist"));
}

// ============================================================
// OSSM Pattern menu — scrollbar presence (7 items > 5)
// ============================================================

void test_ossm_pattern_menu_has_scrollbar(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {ossmPatternItems, ossmPatternCount, 3};
    ui::drawMenu(canvas, data);

    uint16_t *buf = canvas.getBuffer();
    int scrollBarX = ui::Display::WIDTH - 6;
    bool hasScrollbar = false;
    for (int y = ui::Display::StatusbarHeight;
         y < ui::Display::HEIGHT && !hasScrollbar; y++) {
        for (int x = scrollBarX; x < ui::Display::WIDTH; x++) {
            if (buf[y * ui::Display::WIDTH + x] != 0) {
                hasScrollbar = true;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE(hasScrollbar);
}

// ============================================================
// OSSM Pattern menu with descriptions
// ============================================================

static ui::MenuItem ossmPatternDescItems[] = {
    {ui::DEVICE_MENU_ITEM, "Simple Stroke",
     ui::icons::researchAndDesireWaves,
     "A basic back-and-forth linear stroke pattern."},
    {ui::DEVICE_MENU_ITEM, "Teasing Pounding",
     ui::icons::researchAndDesireWaves,
     "Alternates between gentle teasing and forceful pounding."},
    {ui::DEVICE_MENU_ITEM, "Robo Stroke",
     ui::icons::researchAndDesireWaves,
     "Mechanical, precise strokes with consistent speed."},
};
static const int ossmPatternDescCount = 3;

void test_ossm_pattern_menu_with_description_first(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {ossmPatternDescItems, ossmPatternDescCount, 0};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "pattern_desc_first"));
}

void test_ossm_pattern_menu_with_description_second(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {ossmPatternDescItems, ossmPatternDescCount, 1};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "pattern_desc_second"));
}

void test_ossm_pattern_menu_with_description_third(void) {
    ui::clearPage(canvas);
    ui::MenuData data = {ossmPatternDescItems, ossmPatternDescCount, 2};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "pattern_desc_third"));
}

// ============================================================
// OSSM single-item pattern menu edge case
// ============================================================

void test_ossm_pattern_menu_single_item(void) {
    ui::clearPage(canvas);
    ui::MenuItem singleItem[] = {
        {ui::DEVICE_MENU_ITEM, "Simple Stroke",
         ui::icons::researchAndDesireWaves},
    };
    ui::MenuData data = {singleItem, 1, 0};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "pattern_single_item"));
}

// ============================================================
// OSSM Device list menu — what the user sees when scanning
// ============================================================

void test_ossm_device_list_single_device(void) {
    ui::clearPage(canvas);
    ui::MenuItem deviceList[] = {
        {ui::DEVICE_MENU_ITEM, "OSSM 2.0",
         ui::icons::bitmap_ble_connect},
    };
    ui::MenuData data = {deviceList, 1, 0};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "device_list_single"));
}

void test_ossm_device_list_multiple_devices(void) {
    ui::clearPage(canvas);
    ui::MenuItem deviceList[] = {
        {ui::DEVICE_MENU_ITEM, "OSSM 2.0", ui::icons::bitmap_ble_connect},
        {ui::DEVICE_MENU_ITEM, "Lovense Hush 2",
         ui::icons::bitmap_ble_connect},
        {ui::DEVICE_MENU_ITEM, "Unknown Device",
         ui::icons::bitmap_ble_connect},
    };
    ui::MenuData data = {deviceList, 3, 0};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "device_list_multiple"));
}

void test_ossm_device_list_no_devices(void) {
    ui::clearPage(canvas);
    ui::MenuItem deviceList[] = {
        {ui::DEVICE_MENU_ITEM, "No devices found",
         ui::icons::bitmap_ble_connect},
    };
    ui::MenuData data = {deviceList, 1, 0};
    ui::drawMenu(canvas, data);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/menus", "device_list_empty"));
}

// ============================================================
// Registration
// ============================================================

void register_ossm_menu_tests() {
    RUN_TEST(test_ossm_pattern_simple_stroke);
    RUN_TEST(test_ossm_pattern_teasing_pounding);
    RUN_TEST(test_ossm_pattern_robo_stroke);
    RUN_TEST(test_ossm_pattern_half_n_half);
    RUN_TEST(test_ossm_pattern_deeper);
    RUN_TEST(test_ossm_pattern_stop_n_go);
    RUN_TEST(test_ossm_pattern_insist);
    RUN_TEST(test_ossm_pattern_menu_has_scrollbar);
    RUN_TEST(test_ossm_pattern_menu_with_description_first);
    RUN_TEST(test_ossm_pattern_menu_with_description_second);
    RUN_TEST(test_ossm_pattern_menu_with_description_third);
    RUN_TEST(test_ossm_pattern_menu_single_item);
    RUN_TEST(test_ossm_device_list_single_device);
    RUN_TEST(test_ossm_device_list_multiple_devices);
    RUN_TEST(test_ossm_device_list_no_devices);
}
