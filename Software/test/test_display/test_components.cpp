#include "test_helpers.h"

// ============================================================
// Scroll bar
// ============================================================

void test_scrollbar_top(void) {
    ui::clearPage(canvas);
    ui::drawScrollBar(canvas, 0, 10);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "components", "scrollbar_top"));
}

void test_scrollbar_middle(void) {
    ui::clearPage(canvas);
    ui::drawScrollBar(canvas, 5, 10);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "components", "scrollbar_middle"));
}

void test_scrollbar_bottom(void) {
    ui::clearPage(canvas);
    ui::drawScrollBar(canvas, 10, 10);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "components", "scrollbar_bottom"));
}

// ============================================================
// Buttons
// ============================================================

void test_button_normal(void) {
    canvas.fillScreen(0x0000);
    ui::drawButton(canvas, "Cancel", 20, 200);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "components", "button_normal"));
}

void test_button_pressed(void) {
    canvas.fillScreen(0x0000);
    ui::drawButton(canvas, "OK", 120, 200, 70, 35, true);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "components", "button_pressed"));
}

void test_button_pair(void) {
    canvas.fillScreen(0x0000);
    ui::drawButton(canvas, "Back", 20, 200);
    ui::drawButton(canvas, "Home", 230, 200);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "components", "button_pair"));
}

// ============================================================
// clearPage
// ============================================================

void test_clearPage_preserves_statusbar(void) {
    canvas.fillScreen(ui::Colors::white);
    ui::clearPage(canvas, false);

    bool statusbarHasContent = false;
    uint16_t *buf = canvas.getBuffer();
    int centerX = ui::Display::WIDTH / 2;
    for (int x = centerX - 10; x < centerX + 10; x++) {
        int idx = 5 * ui::Display::WIDTH + x;
        if (buf[idx] != 0) {
            statusbarHasContent = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(statusbarHasContent);
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "components", "clearpage_preserve_statusbar"));
}

void test_clearPage_full(void) {
    canvas.fillScreen(ui::Colors::white);
    ui::clearPage(canvas, true);

    bool allBlack = true;
    uint16_t *buf = canvas.getBuffer();
    for (int i = 0; i < 320 * 240; i++) {
        if (buf[i] != 0) {
            allBlack = false;
            break;
        }
    }
    TEST_ASSERT_TRUE(allBlack);
}

// ============================================================
// Registration
// ============================================================

void register_component_tests() {
    RUN_TEST(test_scrollbar_top);
    RUN_TEST(test_scrollbar_middle);
    RUN_TEST(test_scrollbar_bottom);
    RUN_TEST(test_button_normal);
    RUN_TEST(test_button_pressed);
    RUN_TEST(test_button_pair);
    RUN_TEST(test_clearPage_preserves_statusbar);
    RUN_TEST(test_clearPage_full);
}
