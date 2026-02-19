#ifndef EMULATOR_DISPLAY_H
#define EMULATOR_DISPLAY_H

#include <U8g2lib.h>

#include "DeviceEmulator.h"

#define OLED_RESET U8X8_PIN_NONE

void initEmulatorDisplay();
void drawEmulatorScreen(const DeviceEmulator &emulator);
void drawLoadingScreen(const char *message);

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled;

#endif
