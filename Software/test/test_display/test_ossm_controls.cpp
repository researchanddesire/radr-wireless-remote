#include "test_helpers.h"

// ============================================================
// Helpers to compose OSSM controller screen elements via ui::
// ============================================================

static const int16_t TAB_Y = ui::Display::StatusbarHeight;
static const int16_t TAB_HEIGHT = 24;
static const int16_t TAB_WIDTH = ui::Display::WIDTH / 3;
static const int16_t BOTTOM_Y = ui::Display::HEIGHT - 30;

static void drawOssmTabs(GFXcanvas16 &gfx, int focusedIndex) {
    uint16_t depthBg = ui::Colors::disabled;
    uint16_t depthFg = ui::Colors::black;
    uint16_t sensBg = ui::Colors::disabled;
    uint16_t sensFg = ui::Colors::black;
    uint16_t strokeBg = ui::Colors::disabled;
    uint16_t strokeFg = ui::Colors::black;

    if (focusedIndex == 0) {
        depthBg = ui::Colors::depth;
        depthFg = ui::Colors::white;
    } else if (focusedIndex == 1) {
        sensBg = ui::Colors::sensation;
        sensFg = ui::Colors::white;
    } else if (focusedIndex == 2) {
        strokeBg = ui::Colors::stroke;
        strokeFg = ui::Colors::white;
    }

    ui::drawButton(gfx, "Depth", 0, TAB_Y, TAB_WIDTH, TAB_HEIGHT, false,
                   depthBg, depthFg);
    ui::drawButton(gfx, "Sensation", TAB_WIDTH, TAB_Y, TAB_WIDTH, TAB_HEIGHT,
                   false, sensBg, sensFg);
    ui::drawButton(gfx, "Stroke", 2 * TAB_WIDTH, TAB_Y, TAB_WIDTH, TAB_HEIGHT,
                   false, strokeBg, strokeFg);
}

static void drawOssmShoulders(GFXcanvas16 &gfx) {
    ui::drawButton(gfx, "<<", -5, -5);
    ui::drawButton(gfx, ">>", ui::Display::WIDTH - 65, -5);
}

static void drawOssmBottomPlaying(GFXcanvas16 &gfx) {
    ui::drawButton(gfx, "Menu", -5, BOTTOM_Y, 90, 35, false,
                   ui::Colors::disabled, ui::Colors::black);
    ui::drawButton(gfx, "Patterns", ui::Display::WIDTH - 85, BOTTOM_Y, 90);
    ui::drawButton(gfx, "Pause", ui::Display::WIDTH / 2 - 60, BOTTOM_Y, 120);
}

static void drawOssmBottomPaused(GFXcanvas16 &gfx) {
    ui::drawButton(gfx, "Menu", -5, BOTTOM_Y, 90, 35, false,
                   ui::Colors::textBackground, ui::Colors::black);
    ui::drawButton(gfx, "Patterns", ui::Display::WIDTH - 85, BOTTOM_Y, 90);
    ui::drawButton(gfx, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y, 120,
                   35, false, ui::Colors::red, ui::Colors::white);
}

static void drawPatternLabel(GFXcanvas16 &gfx, const std::string &name,
                             uint16_t color = ui::Colors::textBackground) {
    gfx.setFont(NULL);
    gfx.setTextColor(color);
    int16_t x1, y1;
    uint16_t w, h;
    gfx.getTextBounds(name.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t textX = (ui::Display::WIDTH - w) / 2;
    gfx.setCursor(textX, ui::Display::HEIGHT - 70);
    gfx.print(name.c_str());
}

static void drawLinearRail(GFXcanvas16 &gfx, float strokePct,
                           float depthPct) {
    int16_t railX = 10;
    int16_t railY = ui::Display::PageHeight - 30;
    int16_t railW = ui::Display::WIDTH - 20;
    int16_t railH = 20;

    gfx.drawRoundRect(railX, railY, railW, railH, 4, ui::Colors::white);

    int borderMargin = 3;
    int innerWidth = railW - (2 * borderMargin);
    int depthRightEdge = (int)(innerWidth * depthPct / 100.0f);
    int strokeExtent = (int)(depthRightEdge * strokePct / 100.0f);
    int fillStart = depthRightEdge - strokeExtent;
    if (fillStart < 0) fillStart = 0;
    int fillWidth = strokeExtent > 0 ? strokeExtent : 1;

    gfx.fillRect(railX + borderMargin + fillStart, railY + 1, fillWidth,
                 railH - 2, ui::Colors::white);
}

// ============================================================
// Full OSSM controller composition — playing state
// ============================================================

void test_ossm_full_playing_depth_focus(void) {
    ui::clearPage(canvas, true);
    drawOssmTabs(canvas, 0);
    drawOssmShoulders(canvas);
    drawOssmBottomPlaying(canvas);
    drawPatternLabel(canvas, "Simple Stroke");
    drawLinearRail(canvas, 50, 10);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "full_playing_depth"));
}

void test_ossm_full_playing_sensation_focus(void) {
    ui::clearPage(canvas, true);
    drawOssmTabs(canvas, 1);
    drawOssmShoulders(canvas);
    drawOssmBottomPlaying(canvas);
    drawPatternLabel(canvas, "Simple Stroke");
    drawLinearRail(canvas, 50, 50);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "full_playing_sensation"));
}

void test_ossm_full_playing_stroke_focus(void) {
    ui::clearPage(canvas, true);
    drawOssmTabs(canvas, 2);
    drawOssmShoulders(canvas);
    drawOssmBottomPlaying(canvas);
    drawPatternLabel(canvas, "Simple Stroke");
    drawLinearRail(canvas, 100, 80);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "full_playing_stroke"));
}

// ============================================================
// Full OSSM controller — paused state
// ============================================================

void test_ossm_full_paused(void) {
    ui::clearPage(canvas, true);
    drawOssmTabs(canvas, 0);
    drawOssmShoulders(canvas);
    drawOssmBottomPaused(canvas);
    drawPatternLabel(canvas, "Paused", ui::Colors::red);
    drawLinearRail(canvas, 50, 10);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "ossm/controls", "full_paused"));
}

// ============================================================
// Linear rail graph — various stroke/depth combinations
// ============================================================

void test_ossm_rail_zero_zero(void) {
    ui::clearPage(canvas);
    drawLinearRail(canvas, 0, 0);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "rail_zero_zero"));
}

void test_ossm_rail_full_full(void) {
    ui::clearPage(canvas);
    drawLinearRail(canvas, 100, 100);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "rail_full_full"));
}

void test_ossm_rail_half_half(void) {
    ui::clearPage(canvas);
    drawLinearRail(canvas, 50, 50);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "rail_half_half"));
}

void test_ossm_rail_high_stroke_low_depth(void) {
    ui::clearPage(canvas);
    drawLinearRail(canvas, 100, 10);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "ossm/controls",
                                    "rail_high_stroke_low_depth"));
}

void test_ossm_rail_low_stroke_high_depth(void) {
    ui::clearPage(canvas);
    drawLinearRail(canvas, 10, 100);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "ossm/controls",
                                    "rail_low_stroke_high_depth"));
}

void test_ossm_rail_full_stroke_quarter_depth(void) {
    ui::clearPage(canvas);
    drawLinearRail(canvas, 100, 25);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "ossm/controls",
                                    "rail_full_stroke_quarter_depth"));
}

// ============================================================
// Pattern label variations
// ============================================================

void test_ossm_pattern_label_simple_stroke(void) {
    ui::clearPage(canvas);
    drawPatternLabel(canvas, "Simple Stroke");
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "label_simple_stroke"));
}

void test_ossm_pattern_label_teasing_pounding(void) {
    ui::clearPage(canvas);
    drawPatternLabel(canvas, "Teasing Pounding");
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "label_teasing_pounding"));
}

void test_ossm_pattern_label_paused(void) {
    ui::clearPage(canvas);
    drawPatternLabel(canvas, "Paused", ui::Colors::red);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "label_paused"));
}

void test_ossm_pattern_label_empty(void) {
    ui::clearPage(canvas);
    drawPatternLabel(canvas, "");
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "label_empty"));
}

// ============================================================
// Full OSSM controller with header bar
// ============================================================

void test_ossm_full_with_header_connected(void) {
    ui::clearPage(canvas, true);
    ui::HeaderBarData header{
        ui::WifiStatus::CONNECTED,
        ui::BleStatus::CONNECTED,
        ui::BatteryStatus::FULL,
    };
    ui::drawHeaderBar(canvas, header);
    drawOssmTabs(canvas, 0);
    drawOssmShoulders(canvas);
    drawOssmBottomPlaying(canvas);
    drawPatternLabel(canvas, "Simple Stroke");
    drawLinearRail(canvas, 50, 30);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "ossm/controls",
                                    "full_with_header_connected"));
}

void test_ossm_full_with_header_ble_only(void) {
    ui::clearPage(canvas, true);
    ui::HeaderBarData header{
        ui::WifiStatus::DISCONNECTED,
        ui::BleStatus::CONNECTED,
        ui::BatteryStatus::MID,
    };
    ui::drawHeaderBar(canvas, header);
    drawOssmTabs(canvas, 1);
    drawOssmShoulders(canvas);
    drawOssmBottomPlaying(canvas);
    drawPatternLabel(canvas, "Robo Stroke");
    drawLinearRail(canvas, 75, 60);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "ossm/controls",
                                    "full_with_header_ble_only"));
}

void test_ossm_full_with_header_low_battery(void) {
    ui::clearPage(canvas, true);
    ui::HeaderBarData header{
        ui::WifiStatus::CONNECTED,
        ui::BleStatus::CONNECTED,
        ui::BatteryStatus::LOW_BATTERY,
    };
    ui::drawHeaderBar(canvas, header);
    drawOssmTabs(canvas, 2);
    drawOssmShoulders(canvas);
    drawOssmBottomPaused(canvas);
    drawPatternLabel(canvas, "Paused", ui::Colors::red);
    drawLinearRail(canvas, 0, 0);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "ossm/controls",
                                    "full_with_header_low_battery"));
}

// ============================================================
// OSSM flow screens (text pages used in OSSM workflow)
// ============================================================

void test_ossm_device_search_screen(void) {
    ui::drawTextPage(canvas, ui::deviceSearchPage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "device_search_screen"));
}

void test_ossm_connecting_screen(void) {
    ui::drawTextPage(canvas, ui::deviceConnectingPage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "connecting_screen"));
}

void test_ossm_device_stop_screen(void) {
    ui::drawTextPage(canvas, ui::deviceStopPage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "device_stop_screen"));
}

void test_ossm_wifi_settings_screen(void) {
    ui::drawTextPage(canvas, ui::wifiSettingsPage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "wifi_settings_screen"));
}

void test_ossm_wifi_connected_screen(void) {
    ui::drawTextPage(canvas, ui::wifiConnectedPage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "wifi_connected_screen"));
}

void test_ossm_update_screen(void) {
    ui::drawTextPage(canvas, ui::updatePage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "update_screen"));
}

void test_ossm_update_done_screen(void) {
    ui::drawTextPage(canvas, ui::updateDonePage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/controls", "update_done_screen"));
}

// ============================================================
// Registration
// ============================================================

void register_ossm_control_tests() {
    RUN_TEST(test_ossm_full_playing_depth_focus);
    RUN_TEST(test_ossm_full_playing_sensation_focus);
    RUN_TEST(test_ossm_full_playing_stroke_focus);
    RUN_TEST(test_ossm_full_paused);
    RUN_TEST(test_ossm_rail_zero_zero);
    RUN_TEST(test_ossm_rail_full_full);
    RUN_TEST(test_ossm_rail_half_half);
    RUN_TEST(test_ossm_rail_high_stroke_low_depth);
    RUN_TEST(test_ossm_rail_low_stroke_high_depth);
    RUN_TEST(test_ossm_rail_full_stroke_quarter_depth);
    RUN_TEST(test_ossm_pattern_label_simple_stroke);
    RUN_TEST(test_ossm_pattern_label_teasing_pounding);
    RUN_TEST(test_ossm_pattern_label_paused);
    RUN_TEST(test_ossm_pattern_label_empty);
    RUN_TEST(test_ossm_full_with_header_connected);
    RUN_TEST(test_ossm_full_with_header_ble_only);
    RUN_TEST(test_ossm_full_with_header_low_battery);
    RUN_TEST(test_ossm_device_search_screen);
    RUN_TEST(test_ossm_connecting_screen);
    RUN_TEST(test_ossm_device_stop_screen);
    RUN_TEST(test_ossm_wifi_settings_screen);
    RUN_TEST(test_ossm_wifi_connected_screen);
    RUN_TEST(test_ossm_update_screen);
    RUN_TEST(test_ossm_update_done_screen);
}
