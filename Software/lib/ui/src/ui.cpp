#include "ui.h"

#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

namespace ui {

static int16_t headerIconX(int iconIndex, int totalIcons = 3) {
    int totalWidth =
        (Display::Icons::Small + Display::Padding::P1) * totalIcons;
    int startX = (Display::WIDTH - totalWidth) / 2;
    return startX +
           (Display::Icons::Small + Display::Padding::P1) * iconIndex;
}

static const int16_t HEADER_ICON_Y = 3;

static const unsigned char *getBleIcon(BleStatus status) {
    switch (status) {
        case BleStatus::CONNECTED:
            return icons::bitmap_ble_connect;
        case BleStatus::ON:
        case BleStatus::SCANNING:
            return icons::bitmap_ble_connect;
        case BleStatus::OFF:
        default:
            return icons::bitmap_ble_connect;
    }
}

static uint16_t getBleColor(BleStatus status) {
    switch (status) {
        case BleStatus::CONNECTED:
            return Colors::green;
        case BleStatus::SCANNING:
            return Colors::white;
        case BleStatus::ON:
            return Colors::white;
        case BleStatus::OFF:
        default:
            return Colors::bgGray600;
    }
}

static const unsigned char *getWifiIcon(WifiStatus status) {
    switch (status) {
        case WifiStatus::CONNECTED:
            return icons::bitmap_wifi;
        case WifiStatus::ERROR:
        case WifiStatus::DISCONNECTED:
        default:
            return icons::bitmap_wifi_off;
    }
}

static uint16_t getWifiColor(WifiStatus status) {
    switch (status) {
        case WifiStatus::CONNECTED:
            return Colors::white;
        case WifiStatus::ERROR:
            return Colors::red;
        case WifiStatus::DISCONNECTED:
        default:
            return Colors::white;
    }
}

static const unsigned char *getBatteryIcon(BatteryStatus status) {
    switch (status) {
        case BatteryStatus::EMPTY:
            return icons::bitmap_battery_empty;
        case BatteryStatus::LOW_BATTERY:
            return icons::bitmap_battery_low;
        case BatteryStatus::MID:
            return icons::bitmap_battery_mid;
        case BatteryStatus::CHARGING:
            return icons::bitmap_battery_charging;
        case BatteryStatus::FULL:
        default:
            return icons::bitmap_battery_full;
    }
}

static uint16_t getBatteryColor(BatteryStatus status) {
    switch (status) {
        case BatteryStatus::CHARGING:
            return Colors::green;
        case BatteryStatus::EMPTY:
            return Colors::red;
        case BatteryStatus::LOW_BATTERY:
        case BatteryStatus::MID:
        case BatteryStatus::FULL:
        default:
            return Colors::white;
    }
}

void drawHeaderBar(Adafruit_GFX &gfx, const HeaderBarData &data) {
    gfx.fillRect(0, 0, Display::WIDTH, Display::StatusbarHeight,
                 Colors::black);

    int16_t bleX = headerIconX(0);
    int16_t wifiX = headerIconX(1);
    int16_t batteryX = headerIconX(2);

    gfx.drawBitmap(bleX, HEADER_ICON_Y, getBleIcon(data.ble),
                   Display::Icons::Small, Display::Icons::Small,
                   getBleColor(data.ble));

    gfx.drawBitmap(wifiX, HEADER_ICON_Y, getWifiIcon(data.wifi),
                   Display::Icons::Small, Display::Icons::Small,
                   getWifiColor(data.wifi));

    gfx.drawBitmap(batteryX, HEADER_ICON_Y, getBatteryIcon(data.battery),
                   Display::Icons::Small, Display::Icons::Small,
                   getBatteryColor(data.battery));
}

void drawTextPage(Adafruit_GFX &gfx, const TextPage &page,
                  const HeaderBarData &header) {
    clearPage(gfx);

    if (page.includeHeader) {
        drawHeaderBar(gfx, header);
    }

    gfx.setFont(&FreeSansBold12pt7b);
    gfx.setTextColor(Colors::white);

    int16_t titleX1, titleY1;
    uint16_t titleWidth, titleHeight;
    gfx.getTextBounds(page.title.c_str(), 0, 0, &titleX1, &titleY1,
                      &titleWidth, &titleHeight);

    int16_t titleX = (Display::WIDTH - titleWidth) / 2;
    int16_t titleY =
        Display::PageY + Display::Padding::P3 - titleY1;
    gfx.setCursor(titleX, titleY);
    gfx.print(page.title.c_str());

    gfx.setFont(&FreeSans9pt7b);
    gfx.setTextColor(Colors::lightGray);

    const int16_t textMargin = Display::Padding::P2;
    int16_t descY = titleY + titleHeight + Display::Padding::P3;

    bool shouldDrawQRCode =
        page.qrValue.length() > 0 && page.qrValue != strings::EMPTY_STRING;

    int qrCodeWidth = 0;
    if (shouldDrawQRCode) {
        qrCodeWidth = drawQRCode(
            gfx, page.qrValue,
            {.y = descY,
             .maxHeight = Display::PageHeight - descY - textMargin});
    }

    wrapText(gfx, page.description,
             {.x = textMargin,
              .y = descY,
              .rightPadding = textMargin + qrCodeWidth});

    const int16_t buttonY = Display::HEIGHT - 30;

    if (page.leftButtonText.length() > 0 &&
        page.leftButtonText != strings::EMPTY_STRING) {
        drawButton(gfx, page.leftButtonText, 20, buttonY);
    }

    if (page.rightButtonText.length() > 0 &&
        page.rightButtonText != strings::EMPTY_STRING) {
        drawButton(gfx, page.rightButtonText, Display::WIDTH - 90, buttonY);
    }
}

void drawButton(Adafruit_GFX &gfx, const std::string &text, int16_t x,
                int16_t y, int16_t w, int16_t h, bool pressed,
                uint16_t bgColor, uint16_t textColor) {
    gfx.fillRect(x, y, w, h, Colors::black);
    gfx.setFont(&FreeSans9pt7b);

    if (!pressed) {
        gfx.fillRoundRect(x, y, w, h, 5, bgColor);
        gfx.setTextColor(textColor);
    } else {
        gfx.fillRoundRect(x, y, w, h, 5, Colors::white);
        gfx.setTextColor(Colors::black);
    }

    int16_t x1, y1;
    uint16_t tw, th;
    gfx.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &tw, &th);

    int16_t textX = x + (w - tw) / 2;
    int16_t textY = y + (h + th) / 2;

    gfx.setCursor(textX, textY);
    gfx.print(text.c_str());
}

static const int scrollWidth = 6;

void drawMenuItem(Adafruit_GFX &gfx, int index, const MenuItem &option,
                  bool selected, int &yOffset, int menuWidth) {
    uint16_t color = option.color > 0 ? option.color : Colors::textForeground;
    uint16_t unfocusedColor =
        option.unfocusedColor > 0 ? option.unfocusedColor
                                  : Colors::textBackground;

    int menuItemHeight = Display::Icons::Small + Display::Padding::P2;
    int menuItemDescriptionHeight = menuItemHeight * 1.5;
    bool shouldDrawDescription = option.description.has_value() && selected;

    int y = Display::StatusbarHeight + Display::Padding::P1 + yOffset;
    int x = Display::Padding::P1;

    gfx.fillRect(x, y, menuWidth, menuItemHeight, Colors::black);

    if (index > 0) {
        gfx.drawFastHLine(x + Display::Padding::P1, y,
                          menuWidth - Display::Padding::P2,
                          Colors::bgGray900);
    }

    if (selected) {
        gfx.fillRoundRect(x, y, menuWidth,
                          shouldDrawDescription ? menuItemDescriptionHeight
                                                : menuItemHeight,
                          3, Colors::bgGray900);
    }

    gfx.setTextColor(selected ? color : unfocusedColor);
    gfx.setFont(&FreeSans9pt7b);

    int padding = Display::Padding::P2;
    int textOffset = 6;

    if (option.bitmap != nullptr) {
        gfx.drawBitmap(x + padding, y + textOffset, option.bitmap,
                       Display::Icons::Small, Display::Icons::Small,
                       selected ? color : unfocusedColor);
    }

    padding += Display::Icons::Small + Display::Padding::P2;
    gfx.setCursor(x + padding, y + textOffset + menuItemHeight / 2);
    gfx.print(option.name.c_str());

    if (shouldDrawDescription) {
        gfx.setFont(NULL);
        gfx.setTextColor(Colors::textForegroundSecondary);
        wrapText(gfx, option.description.value(),
                 {.x = x + Display::Padding::P2,
                  .y = y + textOffset + Display::Icons::Small +
                       Display::Padding::P0,
                  .rightPadding = Display::Padding::P3});
    }

    yOffset +=
        shouldDrawDescription ? menuItemDescriptionHeight : menuItemHeight;
}

void drawMenu(Adafruit_GFX &gfx, const MenuData &data) {
    int menuWidth =
        Display::WIDTH - scrollWidth - Display::Padding::P1 * 2;
    int menuItemHeight = Display::Icons::Small + Display::Padding::P2;

    int yOffset = 0;

    int safeCurrentOption = data.selectedIndex;
    if (safeCurrentOption < 0) safeCurrentOption = 0;
    if (safeCurrentOption >= data.count) safeCurrentOption = data.count - 1;

    if (data.count <= 5) {
        for (int i = 0; i < data.count; i++) {
            bool isSelected = i == safeCurrentOption;
            drawMenuItem(gfx, i, data.items[i], isSelected, yOffset,
                         menuWidth);
        }
        return;
    }

    drawScrollBar(gfx, safeCurrentOption, data.count - 1);

    for (int i = 0; i < 5; i++) {
        int optionIndex = safeCurrentOption - 2 + i;

        if (optionIndex < 0 || optionIndex >= data.count) {
            int y = Display::StatusbarHeight + Display::Padding::P1 + yOffset;
            int x = Display::Padding::P1;
            gfx.fillRect(x, y, menuWidth, menuItemHeight, Colors::black);
            yOffset += menuItemHeight;
            continue;
        }

        bool isSelected = optionIndex == safeCurrentOption;
        drawMenuItem(gfx, i, data.items[optionIndex], isSelected, yOffset,
                     menuWidth);
    }
}

}  // namespace ui
