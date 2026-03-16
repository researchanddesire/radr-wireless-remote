#include "test_helpers.h"

// ============================================================
// Helpers for generic ButtplugIO device layout
// Bar charts rendered via ui:: primitives since the firmware
// BarChart component draws to hardware tft directly.
// ============================================================

static const int16_t BOTTOM_Y = ui::Display::HEIGHT - 30;

struct BarChartLayout {
    const char *label;
    float value;
    float minValue;
    float maxValue;
    bool focused;
};

static void drawBarChart(GFXcanvas16 &gfx, const BarChartLayout &bar,
                         int16_t x, int16_t y, int16_t w, int16_t h) {
    const int LABEL_WIDTH = 80;
    const int VALUE_WIDTH = 40;
    const int BAR_MARGIN = 4;
    const int CORNER_RADIUS = 3;

    uint16_t fgColor =
        bar.focused ? ui::Colors::sensation : ui::Colors::bgGray600;
    uint16_t textColor = bar.focused ? ui::Colors::textForeground
                                     : ui::Colors::textForegroundSecondary;

    gfx.fillRect(x, y, w, h, ui::Colors::black);

    gfx.setFont(NULL);
    gfx.setTextColor(textColor, ui::Colors::black);
    gfx.setCursor(x + 2, y + (h / 2) - 3);
    gfx.print(bar.label);

    int barX = x + LABEL_WIDTH;
    int barW = w - LABEL_WIDTH - VALUE_WIDTH;
    int barInnerW = barW - 2 * BAR_MARGIN;
    int barY = y + BAR_MARGIN;
    int barH = h - 2 * BAR_MARGIN;

    gfx.drawRoundRect(barX + BAR_MARGIN, barY, barInnerW, barH, CORNER_RADIUS,
                      fgColor);

    int range = (int)(bar.maxValue - bar.minValue);
    int fillW = 0;
    if (range > 0) {
        fillW =
            (int)((long)((int)bar.value - (int)bar.minValue) * barInnerW / range);
    }
    if (fillW < 0) fillW = 0;
    if (fillW > barInnerW) fillW = barInnerW;
    if (fillW > 0) {
        gfx.fillRect(barX + BAR_MARGIN + 1, barY + 1, fillW - 1, barH - 2,
                     fgColor);
    }

    int valX = x + w - VALUE_WIDTH;
    gfx.setTextColor(textColor, ui::Colors::black);
    gfx.setCursor(valX + 4, y + (h / 2) - 3);
    gfx.print((int)bar.value);
}

static void drawBarChartStack(GFXcanvas16 &gfx, BarChartLayout *bars,
                              int count, int focusedIndex) {
    int barHeight = 22;
    int barGap = 4;
    int totalBarsHeight = count * barHeight + (count - 1) * barGap;
    int startY = ui::Display::PageY +
                 (ui::Display::PageHeight - 35 - totalBarsHeight) / 2;
    if (startY < (int)ui::Display::PageY + 4) {
        startY = ui::Display::PageY + 4;
    }
    int barWidth = ui::Display::WIDTH - 10;
    int barX = 5;

    for (int i = 0; i < count; i++) {
        bars[i].focused = (i == focusedIndex);
        int yPos = startY + i * (barHeight + barGap);
        drawBarChart(gfx, bars[i], barX, yPos, barWidth, barHeight);
    }
}

// ============================================================
// Single-feature device (e.g. single vibrator)
// ============================================================

void test_generic_single_feature_zero(void) {
    ui::clearPage(canvas);
    BarChartLayout bars[] = {{"Vibrate", 0, 0, 20, true}};
    drawBarChartStack(canvas, bars, 1, 0);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/controls", "single_feature_zero"));
}

void test_generic_single_feature_mid(void) {
    ui::clearPage(canvas);
    BarChartLayout bars[] = {{"Vibrate", 10, 0, 20, true}};
    drawBarChartStack(canvas, bars, 1, 0);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/controls", "single_feature_mid"));
}

void test_generic_single_feature_max(void) {
    ui::clearPage(canvas);
    BarChartLayout bars[] = {{"Vibrate", 20, 0, 20, true}};
    drawBarChartStack(canvas, bars, 1, 0);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/controls", "single_feature_max"));
}

// Single feature has no shoulder buttons
void test_generic_single_feature_no_shoulders(void) {
    ui::clearPage(canvas);
    BarChartLayout bars[] = {{"Vibrate", 5, 0, 20, true}};
    drawBarChartStack(canvas, bars, 1, 0);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);

    uint16_t *buf = canvas.getBuffer();
    bool topLeftHasContent = false;
    for (int y = 0; y < 30; y++) {
        for (int x = 0; x < 70; x++) {
            if (buf[y * ui::Display::WIDTH + x] != 0) {
                topLeftHasContent = true;
                break;
            }
        }
        if (topLeftHasContent) break;
    }
    TEST_ASSERT_FALSE(topLeftHasContent);
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/controls", "single_no_shoulders"));
}

// ============================================================
// Dual-feature device (e.g. vibrate + rotate)
// ============================================================

void test_generic_dual_feature_first_focused(void) {
    ui::clearPage(canvas);
    BarChartLayout bars[] = {
        {"Vibrate", 8, 0, 20, false},
        {"Rotate", 0, 0, 20, false},
    };
    drawBarChartStack(canvas, bars, 2, 0);
    ui::drawButton(canvas, "<<", -5, -5);
    ui::drawButton(canvas, ">>", ui::Display::WIDTH - 65, -5);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "generic/controls",
                                    "dual_feature_first_focused"));
}

void test_generic_dual_feature_second_focused(void) {
    ui::clearPage(canvas);
    BarChartLayout bars[] = {
        {"Vibrate", 8, 0, 20, false},
        {"Rotate", 12, 0, 20, false},
    };
    drawBarChartStack(canvas, bars, 2, 1);
    ui::drawButton(canvas, "<<", -5, -5);
    ui::drawButton(canvas, ">>", ui::Display::WIDTH - 65, -5);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "generic/controls",
                                    "dual_feature_second_focused"));
}

// ============================================================
// Triple-feature device
// ============================================================

void test_generic_triple_feature(void) {
    ui::clearPage(canvas);
    BarChartLayout bars[] = {
        {"Vibrate", 15, 0, 20, false},
        {"Rotate", 5, 0, 20, false},
        {"Pump", 0, 0, 3, false},
    };
    drawBarChartStack(canvas, bars, 3, 0);
    ui::drawButton(canvas, "<<", -5, -5);
    ui::drawButton(canvas, ">>", ui::Display::WIDTH - 65, -5);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/controls", "triple_feature"));
}

void test_generic_triple_feature_third_focused(void) {
    ui::clearPage(canvas);
    BarChartLayout bars[] = {
        {"Vibrate", 15, 0, 20, false},
        {"Rotate", 5, 0, 20, false},
        {"Pump", 2, 0, 3, false},
    };
    drawBarChartStack(canvas, bars, 3, 2);
    ui::drawButton(canvas, "<<", -5, -5);
    ui::drawButton(canvas, ">>", ui::Display::WIDTH - 65, -5);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "generic/controls",
                                    "triple_feature_third_focused"));
}

// ============================================================
// Max features (6 — the MAX_DISPLAY_FEATURES limit)
// ============================================================

void test_generic_max_features(void) {
    ui::clearPage(canvas);
    BarChartLayout bars[] = {
        {"Vibrate1", 10, 0, 20, false}, {"Vibrate2", 5, 0, 20, false},
        {"Rotate", 3, 0, 20, false},    {"Pump", 1, 0, 3, false},
        {"Linear", 8, 0, 100, false},   {"Constrict", 0, 0, 5, false},
    };
    drawBarChartStack(canvas, bars, 6, 0);
    ui::drawButton(canvas, "<<", -5, -5);
    ui::drawButton(canvas, ">>", ui::Display::WIDTH - 65, -5);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/controls", "max_features"));
}

void test_generic_max_features_last_focused(void) {
    ui::clearPage(canvas);
    BarChartLayout bars[] = {
        {"Vibrate1", 10, 0, 20, false}, {"Vibrate2", 5, 0, 20, false},
        {"Rotate", 3, 0, 20, false},    {"Pump", 1, 0, 3, false},
        {"Linear", 8, 0, 100, false},   {"Constrict", 4, 0, 5, false},
    };
    drawBarChartStack(canvas, bars, 6, 5);
    ui::drawButton(canvas, "<<", -5, -5);
    ui::drawButton(canvas, ">>", ui::Display::WIDTH - 65, -5);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/controls", "max_features_last_focused"));
}

// ============================================================
// Bar chart individual tests — focused vs unfocused
// ============================================================

void test_barchart_focused_at_zero(void) {
    ui::clearPage(canvas);
    BarChartLayout bar = {"Vibrate", 0, 0, 20, true};
    drawBarChart(canvas, bar, 5, 100, ui::Display::WIDTH - 10, 22);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/barchart", "focused_zero"));
}

void test_barchart_focused_at_half(void) {
    ui::clearPage(canvas);
    BarChartLayout bar = {"Vibrate", 10, 0, 20, true};
    drawBarChart(canvas, bar, 5, 100, ui::Display::WIDTH - 10, 22);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/barchart", "focused_half"));
}

void test_barchart_focused_at_max(void) {
    ui::clearPage(canvas);
    BarChartLayout bar = {"Vibrate", 20, 0, 20, true};
    drawBarChart(canvas, bar, 5, 100, ui::Display::WIDTH - 10, 22);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/barchart", "focused_max"));
}

void test_barchart_unfocused_at_half(void) {
    ui::clearPage(canvas);
    BarChartLayout bar = {"Vibrate", 10, 0, 20, false};
    drawBarChart(canvas, bar, 5, 100, ui::Display::WIDTH - 10, 22);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/barchart", "unfocused_half"));
}

void test_barchart_unfocused_at_max(void) {
    ui::clearPage(canvas);
    BarChartLayout bar = {"Vibrate", 20, 0, 20, false};
    drawBarChart(canvas, bar, 5, 100, ui::Display::WIDTH - 10, 22);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/barchart", "unfocused_max"));
}

void test_barchart_small_range(void) {
    ui::clearPage(canvas);
    BarChartLayout bar = {"Pump", 2, 0, 3, true};
    drawBarChart(canvas, bar, 5, 100, ui::Display::WIDTH - 10, 22);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/barchart", "small_range"));
}

void test_barchart_large_range(void) {
    ui::clearPage(canvas);
    BarChartLayout bar = {"Linear", 50, 0, 100, true};
    drawBarChart(canvas, bar, 5, 100, ui::Display::WIDTH - 10, 22);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/barchart", "large_range"));
}

void test_barchart_long_label(void) {
    ui::clearPage(canvas);
    BarChartLayout bar = {"Constrict", 3, 0, 5, true};
    drawBarChart(canvas, bar, 5, 100, ui::Display::WIDTH - 10, 22);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/barchart", "long_label"));
}

// ============================================================
// Generic device STOP button (same as OSSM but standalone)
// ============================================================

void test_generic_stop_button_normal(void) {
    canvas.fillScreen(0x0000);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y,
                   120);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/buttons", "stop_normal"));
}

void test_generic_stop_button_pressed(void) {
    canvas.fillScreen(0x0000);
    ui::drawButton(canvas, "STOP", ui::Display::WIDTH / 2 - 60, BOTTOM_Y, 120,
                   35, true);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "generic/buttons", "stop_pressed"));
}

// ============================================================
// Registration
// ============================================================

void register_generic_device_tests() {
    RUN_TEST(test_generic_single_feature_zero);
    RUN_TEST(test_generic_single_feature_mid);
    RUN_TEST(test_generic_single_feature_max);
    RUN_TEST(test_generic_single_feature_no_shoulders);
    RUN_TEST(test_generic_dual_feature_first_focused);
    RUN_TEST(test_generic_dual_feature_second_focused);
    RUN_TEST(test_generic_triple_feature);
    RUN_TEST(test_generic_triple_feature_third_focused);
    RUN_TEST(test_generic_max_features);
    RUN_TEST(test_generic_max_features_last_focused);
    RUN_TEST(test_barchart_focused_at_zero);
    RUN_TEST(test_barchart_focused_at_half);
    RUN_TEST(test_barchart_focused_at_max);
    RUN_TEST(test_barchart_unfocused_at_half);
    RUN_TEST(test_barchart_unfocused_at_max);
    RUN_TEST(test_barchart_small_range);
    RUN_TEST(test_barchart_large_range);
    RUN_TEST(test_barchart_long_label);
    RUN_TEST(test_generic_stop_button_normal);
    RUN_TEST(test_generic_stop_button_pressed);
}
