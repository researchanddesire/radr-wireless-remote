#include "displayUtils.h"

#include "services/display.h"
#include <ui.h>

static const int scrollWidth = 6;
static const int scrollHeight = Display::HEIGHT - Display::StatusbarHeight;

void drawScrollBar(int currentOption, int numOptions) {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        ui::drawScrollBar(tft, currentOption, numOptions);
        xSemaphoreGive(displayMutex);
    }
}

void clearScreen() {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        ui::clearScreen(tft);
        xSemaphoreGive(displayMutex);
    }
}

void clearPage(bool clearStatusbar) {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        ui::clearPage(tft, clearStatusbar);
        xSemaphoreGive(displayMutex);
    }
}

void wrapText(Adafruit_GFX &gfx, const String &text,
              const WrapTextProps &props) {
    ui::wrapText(gfx, std::string(text.c_str()),
                 {.x = props.x, .y = props.y, .rightPadding = props.rightPadding});
}

int drawQRCode(Adafruit_GFX &gfx, const String &qrValue,
               const DrawQRCodeProps &props) {
    return ui::drawQRCode(
        gfx, std::string(qrValue.c_str()),
        {.x = props.x, .y = props.y, .maxWidth = props.maxWidth, .maxHeight = props.maxHeight});
}
