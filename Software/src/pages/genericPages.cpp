#include "pages/genericPages.h"

#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <components/TextButton.h>
#include <pins.h>
#include <services/display.h>
#include <ui.h>
#include <constants/Sizes.h>

void drawPageTask(void *pvParameters) {
    TextPage *params = static_cast<TextPage *>(pvParameters);

    if (params == nullptr) {
        vTaskDelete(NULL);
        return;
    }

    // Convert firmware TextPage (Arduino String) to ui::TextPage (std::string)
    ui::TextPage uiPage;
    uiPage.title = std::string(params->title.c_str());
    uiPage.description = std::string(params->description.c_str());
    uiPage.qrValue = std::string(params->qrValue.c_str());

    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ui::drawTextPage(tft, uiPage);
        xSemaphoreGive(displayMutex);
    }

    // Position buttons at the very bottom of the screen
    const int16_t buttonY = Display::HEIGHT - 30;

    // Left button
    if (params->leftButtonText.length() > 0 &&
        params->leftButtonText != ui::strings::EMPTY_STRING) {
        TextButton leftButton(params->leftButtonText, pins::BTN_UNDER_L, 20,
                              buttonY);
        leftButton.tick();
    }

    // Right button
    if (params->rightButtonText.length() > 0 &&
        params->rightButtonText != ui::strings::EMPTY_STRING) {
        TextButton rightButton(params->rightButtonText, pins::BTN_UNDER_R,
                               Display::WIDTH - 90, buttonY);
        rightButton.tick();
    }

    vTaskDelete(NULL);
}

void updateStatusText(const String &statusMessage) {
    static bool updateInProgress = false;

    if (updateInProgress) {
        return;
    }

    updateInProgress = true;

    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int16_t statusAreaY = Display::PageY + 55;
        int16_t statusAreaHeight = Display::HEIGHT - statusAreaY - 40;
        tft.fillRect(0, statusAreaY, Display::WIDTH, statusAreaHeight,
                     ST77XX_BLACK);

        tft.setFont(&FreeSans9pt7b);
        tft.setTextColor(ST77XX_WHITE);

        const int16_t textMargin = Display::Padding::P2;
        int16_t currentY = statusAreaY + 20;

        ui::wrapText(tft, std::string(statusMessage.c_str()),
                     {.x = textMargin, .y = currentY});

        xSemaphoreGive(displayMutex);
    }

    updateInProgress = false;
}
