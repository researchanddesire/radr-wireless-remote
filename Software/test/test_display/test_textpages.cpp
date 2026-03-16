#include "test_helpers.h"

// ============================================================
// Static page definitions
// ============================================================

void test_textpage_updatePage(void) {
    ui::drawTextPage(canvas, ui::updatePage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "update"));
}

void test_textpage_updateFilesystemPage(void) {
    ui::drawTextPage(canvas, ui::updateFilesystemPage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "update_filesystem"));
}

void test_textpage_updateSoftwarePage(void) {
    ui::drawTextPage(canvas, ui::updateSoftwarePage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "update_software"));
}

void test_textpage_updateDonePage(void) {
    ui::drawTextPage(canvas, ui::updateDonePage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "update_done"));
}

void test_textpage_deviceSearchPage(void) {
    ui::drawTextPage(canvas, ui::deviceSearchPage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "device_search"));
}

void test_textpage_deviceConnectingPage(void) {
    ui::drawTextPage(canvas, ui::deviceConnectingPage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "device_connecting"));
}

void test_textpage_deviceStopPage(void) {
    ui::drawTextPage(canvas, ui::deviceStopPage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "device_stop"));
}

void test_textpage_wifiSettingsPage(void) {
    ui::drawTextPage(canvas, ui::wifiSettingsPage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "wifi_settings"));
}

void test_textpage_wifiConnectedPage(void) {
    ui::drawTextPage(canvas, ui::wifiConnectedPage);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "wifi_connected"));
}

// ============================================================
// Custom / edge-case pages
// ============================================================

void test_textpage_empty(void) {
    ui::TextPage page;
    ui::drawTextPage(canvas, page);
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "empty"));
}

void test_textpage_titleOnly(void) {
    ui::TextPage page;
    page.title = "Standalone Title";
    ui::drawTextPage(canvas, page);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "title_only"));
}

void test_textpage_longDescription(void) {
    ui::TextPage page;
    page.title = "Information";
    page.description =
        "This is a very long description that should wrap across multiple "
        "lines on the 320x240 TFT display. It contains many words and "
        "should exercise the word-wrap logic extensively to ensure nothing "
        "crashes or overflows the display buffer.";
    ui::drawTextPage(canvas, page);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "long_description"));
}

void test_textpage_bothButtons(void) {
    ui::TextPage page;
    page.title = "Confirm Action";
    page.description = "Are you sure you want to proceed?";
    page.leftButtonText = "Cancel";
    page.rightButtonText = "OK";
    ui::drawTextPage(canvas, page);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "both_buttons"));
}

void test_textpage_withQR(void) {
    ui::TextPage page;
    page.title = "Setup WiFi";
    page.description = "Scan the QR code to connect.";
    page.qrValue = "WIFI:S:TestNetwork;T:WPA;P:password123;;";
    page.leftButtonText = "Back";
    ui::drawTextPage(canvas, page);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages", "with_qr"));
}

// ============================================================
// Pages with header bar (includeHeader = true)
// ============================================================

void test_textpage_with_header_default(void) {
    ui::TextPage page;
    page.title = "With Header";
    page.description = "This page includes a header bar.";
    page.includeHeader = true;
    ui::drawTextPage(canvas, page);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "textpages/with_header", "default"));
}

void test_textpage_with_header_all_connected(void) {
    ui::TextPage page;
    page.title = "All Connected";
    page.description = "WiFi, BLE, and battery all in good state.";
    page.leftButtonText = "Back";
    page.includeHeader = true;

    ui::HeaderBarData header{
        ui::WifiStatus::CONNECTED,
        ui::BleStatus::CONNECTED,
        ui::BatteryStatus::FULL,
    };
    ui::drawTextPage(canvas, page, header);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "textpages/with_header", "all_connected"));
}

void test_textpage_with_header_disconnected(void) {
    ui::TextPage page;
    page.title = "No Connection";
    page.description = "All connectivity is lost.";
    page.leftButtonText = "Retry";
    page.includeHeader = true;

    ui::HeaderBarData header{
        ui::WifiStatus::ERROR,
        ui::BleStatus::OFF,
        ui::BatteryStatus::LOW_BATTERY,
    };
    ui::drawTextPage(canvas, page, header);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "textpages/with_header", "disconnected"));
}

void test_textpage_with_header_and_qr(void) {
    ui::TextPage page;
    page.title = "WiFi Settings";
    page.description =
        "Join the network called 'RADR Setup' to configure WiFi.";
    page.qrValue = "WIFI:S:RADR Setup;T:nopass;;";
    page.leftButtonText = "Back";
    page.includeHeader = true;

    ui::HeaderBarData header{
        ui::WifiStatus::DISCONNECTED,
        ui::BleStatus::ON,
        ui::BatteryStatus::MID,
    };
    ui::drawTextPage(canvas, page, header);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "textpages/with_header", "with_qr"));
}

void test_textpage_with_header_both_buttons(void) {
    ui::TextPage page;
    page.title = "Confirm";
    page.description = "Are you sure?";
    page.leftButtonText = "Cancel";
    page.rightButtonText = "OK";
    page.includeHeader = true;

    ui::HeaderBarData header{
        ui::WifiStatus::CONNECTED,
        ui::BleStatus::SCANNING,
        ui::BatteryStatus::CHARGING,
    };
    ui::drawTextPage(canvas, page, header);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(
        savePPMGrouped(canvas, "textpages/with_header", "both_buttons"));
}

void test_textpage_with_header_long_description(void) {
    ui::TextPage page;
    page.title = "Information";
    page.description =
        "This is a very long description that should wrap across multiple "
        "lines on the 320x240 TFT display. The header bar should be "
        "visible above this content, and the text should still fit within "
        "the remaining page area without overlapping.";
    page.includeHeader = true;

    ui::HeaderBarData header{
        ui::WifiStatus::CONNECTED,
        ui::BleStatus::CONNECTED,
        ui::BatteryStatus::FULL,
    };
    ui::drawTextPage(canvas, page, header);
    TEST_ASSERT_TRUE(bufferHasContent(canvas));
    TEST_ASSERT_TRUE(savePPMGrouped(canvas, "textpages/with_header",
                                    "long_description"));
}

void test_textpage_without_header_has_no_icons(void) {
    ui::TextPage page;
    page.title = "No Header";
    page.description = "Header should not be drawn.";
    page.includeHeader = false;
    ui::drawTextPage(canvas, page);

    uint16_t *buf = canvas.getBuffer();
    int centerX = ui::Display::WIDTH / 2;

    bool statusbarHasContent = false;
    for (int y = 0; y < ui::Display::StatusbarHeight; y++) {
        for (int x = centerX - 40; x < centerX + 40; x++) {
            if (buf[y * ui::Display::WIDTH + x] != 0) {
                statusbarHasContent = true;
                break;
            }
        }
        if (statusbarHasContent) break;
    }
    TEST_ASSERT_FALSE(statusbarHasContent);
}

void test_textpage_with_header_has_icons(void) {
    ui::TextPage page;
    page.title = "With Header";
    page.description = "Header icons should be present.";
    page.includeHeader = true;

    ui::HeaderBarData header{
        ui::WifiStatus::CONNECTED,
        ui::BleStatus::CONNECTED,
        ui::BatteryStatus::FULL,
    };
    ui::drawTextPage(canvas, page, header);

    uint16_t *buf = canvas.getBuffer();
    int centerX = ui::Display::WIDTH / 2;

    bool statusbarHasContent = false;
    for (int y = 0; y < ui::Display::StatusbarHeight; y++) {
        for (int x = centerX - 40; x < centerX + 40; x++) {
            if (buf[y * ui::Display::WIDTH + x] != 0) {
                statusbarHasContent = true;
                break;
            }
        }
        if (statusbarHasContent) break;
    }
    TEST_ASSERT_TRUE(statusbarHasContent);
}

// ============================================================
// Registration
// ============================================================

void register_textpage_tests() {
    RUN_TEST(test_textpage_updatePage);
    RUN_TEST(test_textpage_updateFilesystemPage);
    RUN_TEST(test_textpage_updateSoftwarePage);
    RUN_TEST(test_textpage_updateDonePage);
    RUN_TEST(test_textpage_deviceSearchPage);
    RUN_TEST(test_textpage_deviceConnectingPage);
    RUN_TEST(test_textpage_deviceStopPage);
    RUN_TEST(test_textpage_wifiSettingsPage);
    RUN_TEST(test_textpage_wifiConnectedPage);
    RUN_TEST(test_textpage_empty);
    RUN_TEST(test_textpage_titleOnly);
    RUN_TEST(test_textpage_longDescription);
    RUN_TEST(test_textpage_bothButtons);
    RUN_TEST(test_textpage_withQR);

    RUN_TEST(test_textpage_with_header_default);
    RUN_TEST(test_textpage_with_header_all_connected);
    RUN_TEST(test_textpage_with_header_disconnected);
    RUN_TEST(test_textpage_with_header_and_qr);
    RUN_TEST(test_textpage_with_header_both_buttons);
    RUN_TEST(test_textpage_with_header_long_description);
    RUN_TEST(test_textpage_without_header_has_no_icons);
    RUN_TEST(test_textpage_with_header_has_icons);
}
