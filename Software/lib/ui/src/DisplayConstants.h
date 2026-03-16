#ifndef UI_DISPLAY_CONSTANTS_H
#define UI_DISPLAY_CONSTANTS_H

#include <stdint.h>

namespace ui {

namespace Display {
constexpr int WIDTH = 320;
constexpr int HEIGHT = 240;

constexpr int P1 = 6;
constexpr int StatusbarIcons = 24;
constexpr int StatusbarNumberOfIcons = 3;
constexpr int StatusbarHeight = StatusbarIcons + P1;
constexpr int StatusbarWidth =
    StatusbarIcons * StatusbarNumberOfIcons + P1 * 2;

constexpr int PageHeight = HEIGHT - StatusbarHeight;
constexpr int PageY = StatusbarHeight;

constexpr int NotificationBarHeight = StatusbarHeight + StatusbarIcons + P1;

namespace Icons {
constexpr int Big = 120;
constexpr int Small = 24;
constexpr int NumIcons = 3;
}  // namespace Icons

namespace Padding {
constexpr int P0 = 3;
constexpr int P1 = 6;
constexpr int P2 = 12;
constexpr int P3 = 18;
constexpr int P4 = 24;
}  // namespace Padding
}  // namespace Display

namespace Colors {
constexpr uint16_t black = 0x0000;
constexpr uint16_t white = 0xFFFF;
constexpr uint16_t red = 0xF800;
constexpr uint16_t dimRed = 0x8000;
constexpr uint16_t green = 0x07E0;
constexpr uint16_t yellow = 0xFFE0;

constexpr uint16_t depth = 0xE186;
constexpr uint16_t sensation = 0x3C9F;
constexpr uint16_t stroke = 0x4E8A;
constexpr uint16_t speed = 0x5013;

constexpr uint16_t disabled = 0x1082;
constexpr uint16_t bgGray900 = 0x10c5;
constexpr uint16_t bgGray600 = 0x4aac;
constexpr uint16_t lightGray = 0x9c71;

constexpr uint16_t textForeground = white;
constexpr uint16_t textForegroundSecondary = 0xDEFB;
constexpr uint16_t textBackground = 0x4aac;
}  // namespace Colors

}  // namespace ui

#endif  // UI_DISPLAY_CONSTANTS_H
