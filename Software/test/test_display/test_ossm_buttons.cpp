#include "test_helpers.h"

// ============================================================
// OSSM Tab buttons (Depth, Sensation, Stroke)
// Tabs span full width, no gaps, equal sizing at top of page
// ============================================================

static const int16_t TAB_Y = ui::Display::StatusbarHeight;
static const int16_t TAB_HEIGHT = 24;
static const int16_t TAB_WIDTH = ui::Display::WIDTH / 3;

void test_ossm_tab_depth(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Depth", 0, TAB_Y, TAB_WIDTH, TAB_HEIGHT, false,
                   ui::Colors::depth, ui::Colors::white);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "ossm/buttons", "tab_depth_active"));
}

void test_ossm_tab_sensation(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Sensation", TAB_WIDTH, TAB_Y, TAB_WIDTH,
                   TAB_HEIGHT, false, ui::Colors::sensation, ui::Colors::white);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "tab_sensation_active"));
}

void test_ossm_tab_stroke(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Stroke", 2 * TAB_WIDTH, TAB_Y, TAB_WIDTH,
                   TAB_HEIGHT, false, ui::Colors::stroke, ui::Colors::white);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "tab_stroke_active"));
}

void test_ossm_tab_depth_inactive(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Depth", 0, TAB_Y, TAB_WIDTH, TAB_HEIGHT, false,
                   ui::Colors::disabled, ui::Colors::black);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "tab_depth_inactive"));
}

void test_ossm_tab_sensation_inactive(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Sensation", TAB_WIDTH, TAB_Y, TAB_WIDTH,
                   TAB_HEIGHT, false, ui::Colors::disabled, ui::Colors::black);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "tab_sensation_inactive"));
}

void test_ossm_tab_stroke_inactive(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Stroke", 2 * TAB_WIDTH, TAB_Y, TAB_WIDTH,
                   TAB_HEIGHT, false, ui::Colors::disabled, ui::Colors::black);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "tab_stroke_inactive"));
}

// All three tabs in each focus state
void test_ossm_tabs_focus_depth(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Depth", 0, TAB_Y, TAB_WIDTH, TAB_HEIGHT, false,
                   ui::Colors::depth, ui::Colors::white);
    ui::drawButton(canvas, "Sensation", TAB_WIDTH, TAB_Y, TAB_WIDTH,
                   TAB_HEIGHT, false, ui::Colors::disabled, ui::Colors::black);
    ui::drawButton(canvas, "Stroke", 2 * TAB_WIDTH, TAB_Y, TAB_WIDTH,
                   TAB_HEIGHT, false, ui::Colors::disabled, ui::Colors::black);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "tabs_focus_depth"));
}

void test_ossm_tabs_focus_sensation(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Depth", 0, TAB_Y, TAB_WIDTH, TAB_HEIGHT, false,
                   ui::Colors::disabled, ui::Colors::black);
    ui::drawButton(canvas, "Sensation", TAB_WIDTH, TAB_Y, TAB_WIDTH,
                   TAB_HEIGHT, false, ui::Colors::sensation, ui::Colors::white);
    ui::drawButton(canvas, "Stroke", 2 * TAB_WIDTH, TAB_Y, TAB_WIDTH,
                   TAB_HEIGHT, false, ui::Colors::disabled, ui::Colors::black);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "tabs_focus_sensation"));
}

void test_ossm_tabs_focus_stroke(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Depth", 0, TAB_Y, TAB_WIDTH, TAB_HEIGHT, false,
                   ui::Colors::disabled, ui::Colors::black);
    ui::drawButton(canvas, "Sensation", TAB_WIDTH, TAB_Y, TAB_WIDTH,
                   TAB_HEIGHT, false, ui::Colors::disabled, ui::Colors::black);
    ui::drawButton(canvas, "Stroke", 2 * TAB_WIDTH, TAB_Y, TAB_WIDTH,
                   TAB_HEIGHT, false, ui::Colors::stroke, ui::Colors::white);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "tabs_focus_stroke"));
}

// ============================================================
// OSSM Shoulder buttons (<< and >>)
// Positioned with negative margin at screen edges
// ============================================================

void test_ossm_shoulder_left(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "<<", -5, -5);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "shoulder_left"));
}

void test_ossm_shoulder_right(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, ">>", ui::Display::WIDTH - 65, -5);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "shoulder_right"));
}

void test_ossm_shoulder_left_pressed(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "<<", -5, -5, 70, 35, true);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "shoulder_left_pressed"));
}

void test_ossm_shoulder_right_pressed(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, ">>", ui::Display::WIDTH - 65, -5, 70, 35, true);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "shoulder_right_pressed"));
}

void test_ossm_shoulders_pair(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "<<", -5, -5);
    ui::drawButton(canvas, ">>", ui::Display::WIDTH - 65, -5);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "shoulders_pair"));
}

// ============================================================
// OSSM Bottom buttons (Menu, Patterns, Pause/STOP)
// ============================================================

static const int16_t BOTTOM_Y = ui::Display::HEIGHT - 30;

void test_ossm_menu_button(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Menu", -5, BOTTOM_Y, 90, 35, false,
                   ui::Colors::textBackground, ui::Colors::black);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "menu_button"));
}

void test_ossm_menu_button_disabled(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Menu", -5, BOTTOM_Y, 90, 35, false,
                   ui::Colors::disabled, ui::Colors::black);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "menu_button_disabled"));
}

void test_ossm_patterns_button(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Patterns", ui::Display::WIDTH - 85, BOTTOM_Y, 90);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "patterns_button"));
}

void test_ossm_pause_button(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Pause", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "pause_button"));
}

void test_ossm_stop_button(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y, 120,
                   35, false, ui::Colors::red, ui::Colors::white);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "ossm/buttons", "stop_button"));
}

void test_ossm_bottom_buttons_playing(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Menu", -5, BOTTOM_Y, 90, 35, false,
                   ui::Colors::disabled, ui::Colors::black);
    ui::drawButton(canvas, "Patterns", ui::Display::WIDTH - 85, BOTTOM_Y, 90);
    ui::drawButton(canvas, "Pause", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "bottom_buttons_playing"));
}

void test_ossm_bottom_buttons_paused(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Menu", -5, BOTTOM_Y, 90, 35, false,
                   ui::Colors::textBackground, ui::Colors::black);
    ui::drawButton(canvas, "Patterns", ui::Display::WIDTH - 85, BOTTOM_Y, 90);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y, 120,
                   35, false, ui::Colors::red, ui::Colors::white);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "bottom_buttons_paused"));
}

// ============================================================
// Button color variations used by the OSSM
// ============================================================

void test_ossm_button_color_depth(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Depth", 20, 100, 100, 35, false,
                   ui::Colors::depth, ui::Colors::white);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "color_depth"));
}

void test_ossm_button_color_sensation(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Sensation", 20, 100, 100, 35, false,
                   ui::Colors::sensation, ui::Colors::white);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "color_sensation"));
}

void test_ossm_button_color_stroke(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Stroke", 20, 100, 100, 35, false,
                   ui::Colors::stroke, ui::Colors::white);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "color_stroke"));
}

void test_ossm_button_color_speed(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Speed", 20, 100, 100, 35, false,
                   ui::Colors::speed, ui::Colors::white);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "color_speed"));
}

void test_ossm_button_color_red(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "STOP", 20, 100, 120, 35, false, ui::Colors::red,
                   ui::Colors::white);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "ossm/buttons", "color_red"));
}

void test_ossm_button_color_disabled(void) {
    ui::clearPage(canvas);
    ui::drawButton(canvas, "Menu", 20, 100, 90, 35, false,
                   ui::Colors::disabled, ui::Colors::black);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "ossm/buttons", "color_disabled"));
}

// ============================================================
// Registration
// ============================================================

void register_ossm_button_tests() {
    RUN_TEST(test_ossm_tab_depth);
    RUN_TEST(test_ossm_tab_sensation);
    RUN_TEST(test_ossm_tab_stroke);
    RUN_TEST(test_ossm_tab_depth_inactive);
    RUN_TEST(test_ossm_tab_sensation_inactive);
    RUN_TEST(test_ossm_tab_stroke_inactive);
    RUN_TEST(test_ossm_tabs_focus_depth);
    RUN_TEST(test_ossm_tabs_focus_sensation);
    RUN_TEST(test_ossm_tabs_focus_stroke);
    RUN_TEST(test_ossm_shoulder_left);
    RUN_TEST(test_ossm_shoulder_right);
    RUN_TEST(test_ossm_shoulder_left_pressed);
    RUN_TEST(test_ossm_shoulder_right_pressed);
    RUN_TEST(test_ossm_shoulders_pair);
    RUN_TEST(test_ossm_menu_button);
    RUN_TEST(test_ossm_menu_button_disabled);
    RUN_TEST(test_ossm_patterns_button);
    RUN_TEST(test_ossm_pause_button);
    RUN_TEST(test_ossm_stop_button);
    RUN_TEST(test_ossm_bottom_buttons_playing);
    RUN_TEST(test_ossm_bottom_buttons_paused);
    RUN_TEST(test_ossm_button_color_depth);
    RUN_TEST(test_ossm_button_color_sensation);
    RUN_TEST(test_ossm_button_color_stroke);
    RUN_TEST(test_ossm_button_color_speed);
    RUN_TEST(test_ossm_button_color_red);
    RUN_TEST(test_ossm_button_color_disabled);
}
