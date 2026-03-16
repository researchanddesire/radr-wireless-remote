#include "DrawUtils.h"

#include <algorithm>

extern "C" {
#include "qrcode.h"
}

namespace ui {

static const int scrollWidth = 6;
static const int scrollHeight = Display::HEIGHT - Display::StatusbarHeight;

void clearScreen(Adafruit_GFX &gfx) {
    gfx.fillScreen(Colors::black);
}

void clearPage(Adafruit_GFX &gfx, bool clearStatusbar) {
    if (clearStatusbar) {
        clearScreen(gfx);
        return;
    }

    gfx.fillRect(0, Display::PageY, Display::WIDTH, Display::PageHeight,
                 Colors::black);

    int cornerWidth = (Display::WIDTH / 2) - (Display::StatusbarWidth / 2);
    gfx.fillRect(0, 0, cornerWidth, Display::StatusbarHeight, Colors::black);
    gfx.fillRect(Display::WIDTH - cornerWidth, 0, cornerWidth,
                 Display::StatusbarHeight, Colors::black);
}

void drawScrollBar(Adafruit_GFX &gfx, int currentOption, int numOptions) {
    float scrollPercent = (float)currentOption / (numOptions);
    int scrollPosition = scrollPercent * (scrollHeight - 20);

    gfx.fillRect(Display::WIDTH - scrollWidth, Display::StatusbarHeight,
                 scrollWidth, scrollHeight, Colors::black);

    gfx.drawFastVLine(Display::WIDTH - (scrollWidth / 2),
                      Display::StatusbarHeight + Display::Padding::P0,
                      scrollHeight - Display::Padding::P1,
                      Colors::bgGray900);

    gfx.fillRoundRect(Display::WIDTH - scrollWidth,
                      Display::StatusbarHeight + scrollPosition, scrollWidth,
                      20, 3, Colors::white);
}

void wrapText(Adafruit_GFX &gfx, const std::string &text,
              const WrapTextProps &props) {
    const int16_t availableWidth = gfx.width() - props.x - props.rightPadding;

    gfx.setCursor(props.x, props.y);

    std::string currentLine;

    auto printAndNewline = [&](const std::string &line) {
        if (line.length() > 0) {
            gfx.print(line.c_str());
        }
        gfx.println("");
        gfx.setCursor(props.x, gfx.getCursorY());
    };

    auto measureWidth = [&](const std::string &s) -> uint16_t {
        int16_t x1, y1;
        uint16_t w, h;
        gfx.getTextBounds(s.c_str(), 0, 0, &x1, &y1, &w, &h);
        return w;
    };

    std::string word;
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];

        if (c == '\n') {
            if (word.length() > 0) {
                std::string trial = currentLine;
                if (trial.length() > 0) trial += ' ';
                trial += word;
                if ((int)measureWidth(trial) <= availableWidth ||
                    currentLine.length() == 0) {
                    currentLine = trial;
                } else {
                    printAndNewline(currentLine);
                    currentLine = word;
                }
                word = "";
            }
            printAndNewline(currentLine);
            currentLine = "";
            continue;
        }

        if (c == ' ') {
            if (word.length() > 0) {
                std::string trial = currentLine;
                if (trial.length() > 0) trial += ' ';
                trial += word;

                if ((int)measureWidth(trial) <= availableWidth ||
                    currentLine.length() == 0) {
                    currentLine = trial;
                } else {
                    printAndNewline(currentLine);
                    currentLine = word;
                }
                word = "";
            }
        } else {
            word += c;

            if (currentLine.length() == 0 &&
                (int)measureWidth(word) > availableWidth) {
                std::string chunk;
                for (size_t j = 0; j < word.length(); ++j) {
                    std::string trial = chunk;
                    trial += word[j];
                    if ((int)measureWidth(trial) > availableWidth &&
                        chunk.length() > 0) {
                        gfx.print(chunk.c_str());
                        printAndNewline("");
                        chunk = "";
                    }
                    chunk += word[j];
                }
                currentLine = chunk;
                word = "";
            }
        }
    }

    if (word.length() > 0) {
        std::string trial = currentLine;
        if (trial.length() > 0) trial += ' ';
        trial += word;
        if ((int)measureWidth(trial) <= availableWidth ||
            currentLine.length() == 0) {
            currentLine = trial;
        } else {
            printAndNewline(currentLine);
            currentLine = word;
        }
    }

    if (currentLine.length() > 0) {
        gfx.print(currentLine.c_str());
    }
}

int drawQRCode(Adafruit_GFX &gfx, const std::string &qrValue,
               const DrawQRCodeProps &props) {
    QRCode qrcode;
    static constexpr uint8_t QR_VERSION = 7;
    static constexpr uint8_t QR_ECC_LOW = 1;

    uint8_t qrcodeData[qrcode_getBufferSize(QR_VERSION)];

    qrcode_initText(&qrcode, qrcodeData, QR_VERSION, QR_ECC_LOW,
                    qrValue.c_str());

    const int16_t widthLimit = std::min<int16_t>(props.x, props.maxWidth);
    const int16_t heightLimit =
        std::min<int16_t>(gfx.height() - props.y, props.maxHeight);
    const int16_t availableWidth = std::max<int16_t>(0, widthLimit);
    const int16_t availableHeight = std::max<int16_t>(0, heightLimit);
    const int16_t maxModuleScaleW =
        availableWidth > 0 ? (availableWidth / qrcode.size) : 0;
    const int16_t maxModuleScaleH =
        availableHeight > 0 ? (availableHeight / qrcode.size) : 0;
    int16_t scale = std::max<int16_t>(
        1, std::min<int16_t>(maxModuleScaleW, maxModuleScaleH));

    if (scale <= 0) {
        scale = 1;
    }

    const int16_t totalSizePx = qrcode.size * scale;
    const int16_t xOffset = props.x - totalSizePx;
    const int16_t yOffset = props.y;

    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                gfx.fillRect(xOffset + x * scale, yOffset + y * scale, scale,
                             scale, Colors::white);
            }
        }
    }

    return totalSizePx;
}

}  // namespace ui
