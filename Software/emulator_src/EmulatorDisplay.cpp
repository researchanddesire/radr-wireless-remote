#include "EmulatorDisplay.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R2, OLED_RESET, 22, 21);

void initEmulatorDisplay() {
    oled.begin();
    oled.setI2CAddress(0x3C << 1);
    oled.setPowerSave(0);
    oled.setContrast(255);
    oled.clearBuffer();
    oled.sendBuffer();
}

void drawLoadingScreen(const char *message) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(0, 10, "ButtplugIO Emulator");
    oled.drawHLine(0, 12, 128);
    oled.setFont(u8g2_font_5x8_tr);
    oled.drawStr(0, 30, message);
    oled.sendBuffer();
}

static void drawFeatureBar(int y, const EmulatedFeature &feat, int maxWidth) {
    oled.setFont(u8g2_font_5x8_tr);

    // Truncate label to 8 chars
    String label = feat.name;
    if (label.length() > 8) label = label.substring(0, 8);

    oled.drawStr(0, y, label.c_str());

    int barX = 50;
    int barW = maxWidth - 75;
    int barH = 6;
    int barY = y - 6;

    oled.drawFrame(barX, barY, barW, barH);

    int range = feat.maxValue - feat.minValue;
    if (range > 0) {
        float normalized =
            (feat.currentValue - feat.minValue) / (float)range;
        int fillW = (int)(normalized * (barW - 2));
        if (fillW > 0) {
            oled.drawBox(barX + 1, barY + 1, fillW, barH - 2);
        }
    }

    String valStr =
        String((int)feat.currentValue) + "/" + String(feat.maxValue);
    int valX = barX + barW + 3;
    oled.drawStr(valX, y, valStr.c_str());
}

void drawEmulatorScreen(const DeviceEmulator &emulator) {
    oled.clearBuffer();

    const DeviceEntry *dev = emulator.getCurrentDevice();
    if (dev == nullptr) {
        oled.setFont(u8g2_font_6x10_tr);
        oled.drawStr(0, 12, "No devices loaded");
        oled.sendBuffer();
        return;
    }

    // Header: device name + index
    oled.setFont(u8g2_font_6x10_tr);
    String header = dev->deviceName;
    if (header.length() > 16) header = header.substring(0, 16);
    oled.drawStr(0, 10, header.c_str());

    String indexStr = "[" + String(emulator.getCurrentIndex() + 1) + "/" +
                      String(emulator.getDeviceCount()) + "]";
    int indexW = indexStr.length() * 6;
    oled.drawStr(128 - indexW, 10, indexStr.c_str());

    oled.drawHLine(0, 12, 128);

    // UUID (truncated)
    oled.setFont(u8g2_font_5x8_tr);
    String uuid = dev->serviceUUID;
    if (uuid.length() > 24) uuid = uuid.substring(0, 24) + "..";
    oled.drawStr(0, 22, uuid.c_str());

    // Connection status
    String status = emulator.isConnected() ? "Connected" : "Advertising...";
    oled.drawStr(0, 31, status.c_str());

    oled.drawHLine(0, 33, 128);

    // Feature bars (up to 4 to fit on screen)
    const auto &features = dev->features;
    int maxFeatures = min((int)features.size(), 4);
    int featureY = 42;
    for (int i = 0; i < maxFeatures; i++) {
        drawFeatureBar(featureY, features[i], 128);
        featureY += 10;
    }

    // Footer hint
    if (featureY < 60) {
        oled.setFont(u8g2_font_5x8_tr);
        oled.drawStr(0, 63, "[BTN] Next device");
    }

    oled.sendBuffer();
}
