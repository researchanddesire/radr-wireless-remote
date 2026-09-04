#include "pages/genericPages.h"
#include "utils/psramTask.h"

#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <components/TextButton.h>
#include <constants/Colors.h>
#include <constants/Sizes.h>
#include <pins.h>
#include <qrcode.h>
#include <services/display.h>

#include "displayUtils.h"

// OSSM page definitions (extern-declared in TextPages.h to avoid 11x static
// duplication across translation units)
const TextPage ossmHelpPage = {
    .title = "OSSM Help",
    .description = "Scan for guides\nand troubleshooting",
    .qrValue = "HTTPS://DOCS.RESEARCHANDDESIRE.COM/OSSM",
    .leftButtonText = GO_BACK,
};

const TextPage ossmRestartConfirmPage = {
    .title = "Restart OSSM?",
    .description =
        "This will restart your OSSM device. The remote will stay on but "
        "disconnect.",
    .leftButtonText = CANCEL_STRING,
    .rightButtonText = "Restart",
};

const TextPage ossmRestartingPage = {
    .title = "Restarting OSSM",
    .description = "Your OSSM is restarting. Please wait...",
};

const TextPage streamingPage = {
    .title = "Streaming Active",
    .description =
        "Your OSSM is in streaming mode. Position commands are being received "
        "from an external source.",
    .leftButtonText = "Menu",
};

void drawPageTask(void *pvParameters) {
    TextPage *params = static_cast<TextPage *>(pvParameters);

    if (params == nullptr) {
        exitPsramTask();
    }

    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Clear the page area first
        clearPage();

        // Draw title with large bold font
        tft.setFont(&FreeSansBold12pt7b);
        tft.setTextColor(Colors::white);

        // Get title bounds for centering
        int16_t titleX1, titleY1;
        uint16_t titleWidth, titleHeight;
        tft.getTextBounds(params->title.c_str(), 0, 0, &titleX1, &titleY1,
                          &titleWidth, &titleHeight);

        // Center title horizontally
        int16_t titleX = (Display::WIDTH - titleWidth) / 2;
        int16_t titleY = Display::PageY + Display::Padding::P3 -
                         titleY1;  // Top padding from page start
        tft.setCursor(titleX, titleY);
        tft.print(params->title);

        // Draw description with smaller font and proper text wrapping
        tft.setFont(&FreeSans9pt7b);
        tft.setTextColor(Colors::lightGray);

        // Calculate text area with margins
        const int16_t textMargin = Display::Padding::P2;
        const int16_t textWidth =
            Display::WIDTH - (2 * textMargin);  // Leave margin on both sides
        int16_t descY = titleY + titleHeight + Display::Padding::P3;

        bool shouldDrawQRCode =
            params->qrValue.length() > 0 && params->qrValue != EMPTY_STRING;

        int qrCodeWidth = 0;
        if (shouldDrawQRCode) {
            qrCodeWidth = drawQRCode(
                tft, params->qrValue,
                {.y = descY,
                 .maxHeight = Display::PageHeight - descY - textMargin});
        }

        wrapText(tft, params->description,
                 {.x = textMargin,
                  .y = descY,
                  .rightPadding = textMargin + qrCodeWidth});

        xSemaphoreGive(displayMutex);
    }

    // Position buttons at the very bottom of the screen with no bottom
    // padding
    const int16_t buttonY =
        Display::HEIGHT -
        30;  // 30px from bottom (exactly button height, no padding)

    // Left button
    if (params->leftButtonText.length() > 0 &&
        params->leftButtonText != EMPTY_STRING) {
        TextButton leftButton(params->leftButtonText, pins::BTN_UNDER_L, 20,
                              buttonY);
        leftButton.tick();
    }

    
    // Right button
    if (params->rightButtonText.length() > 0 &&
        params->rightButtonText != EMPTY_STRING) {
        TextButton rightButton(params->rightButtonText, pins::BTN_UNDER_R,
                               Display::WIDTH - 90, buttonY);
        rightButton.tick();
    }

    // Clean up dynamically allocated parameters if they came from
    // updateStatusText For now, just avoid the delete to prevent crashes -
    // memory leak is better than crash delete params;
    exitPsramTask();
}

void updateStatusText(const String &statusMessage) {
    // Simple flag to prevent multiple concurrent status updates
    static bool updateInProgress = false;

    if (updateInProgress) {
        return;  // Skip if update already in progress
    }

    updateInProgress = true;

    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Clear only a specific area for the status text to avoid overlapping
        // Clear from middle of screen down to button area
        int16_t statusAreaY =
            Display::PageY + 55;  // Start below title/description area
        int16_t statusAreaHeight =
            Display::HEIGHT - statusAreaY - 40;  // Leave room for buttons
        tft.fillRect(0, statusAreaY, Display::WIDTH, statusAreaHeight,
                     ST77XX_BLACK);

        // Draw status message with proper text wrapping
        tft.setFont(&FreeSans9pt7b);
        tft.setTextColor(ST77XX_WHITE);

        // Calculate text area with margins
        const int16_t textMargin = Display::Padding::P2;
        const int16_t textWidth = Display::WIDTH - (2 * textMargin);
        int16_t currentY = statusAreaY + 20;
        const int16_t lineHeight =
            18;  // Approximate line height for FreeSans9pt7b

        // Simple word wrapping for status message
        String text = statusMessage;

        while (text.length() > 0) {
            String line = "";
            int16_t lineWidth = 0;
            int lastSpace = -1;

            // Build line word by word until it would exceed the available width
            for (int i = 0; i < text.length(); i++) {
                char c = text.charAt(i);
                if (c == ' ') lastSpace = i;

                String testLine = line + c;
                int16_t x1, y1;
                uint16_t w, h;
                tft.getTextBounds(testLine.c_str(), 0, 0, &x1, &y1, &w, &h);

                if (w > textWidth && lastSpace > 0) {
                    // Line would be too long, break at last space
                    line = text.substring(0, lastSpace);
                    text = text.substring(lastSpace + 1);
                    break;
                } else if (i == text.length() - 1) {
                    // Last character, use the whole line
                    line = testLine;
                    text = "";
                    break;
                } else {
                    line = testLine;
                }
            }

            // If no space was found and line is too long, force break
            if (line.length() == 0 && text.length() > 0) {
                line = text.substring(0, 1);
                text = text.substring(1);
            }

            // Draw the line with proper margin
            tft.setCursor(textMargin, currentY);
            tft.print(line);
            currentY += lineHeight;
        }

        xSemaphoreGive(displayMutex);
    }

    updateInProgress = false;
}