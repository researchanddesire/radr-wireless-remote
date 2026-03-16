#include "test_helpers.h"

// ============================================================
// QR code rendering
// ============================================================

void test_qr_wifi(void) {
    canvas.fillScreen(0x0000);
    ui::drawQRCode(canvas, "WIFI:S:RADR Setup;T:nopass;;");
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "qr", "wifi"));
}

void test_qr_url(void) {
    canvas.fillScreen(0x0000);
    ui::drawQRCode(canvas, "HTTPS://DASHBOARD.RESEARCHANDDESIRE.COM");
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "qr", "url"));
}

void test_qr_short(void) {
    canvas.fillScreen(0x0000);
    ui::drawQRCode(canvas, "TEST");
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "qr", "short"));
}

void test_qr_custom_position(void) {
    canvas.fillScreen(0x0000);
    ui::DrawQRCodeProps props;
    props.x = 160;
    props.y = 60;
    props.maxHeight = 120;
    ui::drawQRCode(canvas, "HTTPS://DOCS.RESEARCHANDDESIRE.COM", props);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "qr", "custom_position"));
}

void test_qr_in_textpage(void) {
    ui::TextPage page;
    page.title = "Pair Device";
    page.description = "Scan the QR code or visit the URL to pair.";
    page.qrValue = "HTTPS://DASHBOARD.RESEARCHANDDESIRE.COM?CODE=AABBCCDD";
    page.leftButtonText = "Back";
    ui::drawTextPage(canvas, page);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "qr", "in_textpage"));
}

// ============================================================
// Registration
// ============================================================

void register_qr_tests() {
    RUN_TEST(test_qr_wifi);
    RUN_TEST(test_qr_url);
    RUN_TEST(test_qr_short);
    RUN_TEST(test_qr_custom_position);
    RUN_TEST(test_qr_in_textpage);
}
