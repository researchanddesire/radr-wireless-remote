#pragma once

#ifndef MENUS_H
#define MENUS_H

#include "structs/Menus.h"
#include "Adafruit_ST7789.h"
#include "components/Icons.h"
#include "constants/Colors.h"
#include "constants/Sizes.h"
#include <Fonts/FreeSans9pt7b.h>
#include "services/display.h"

extern std::vector<MenuItem> *activeMenu;
extern int activeMenuCount;
extern int currentOption;
extern int activeTab;      // 0=OSSM, 1=RADR
extern int tabBarHeight;   // 0 when no tabs, ~28 when tabs shown

extern TaskHandle_t menuTaskHandle;

void drawMenu();
void stopMenuTask();
void drawMenuWithTabs();
void drawTabBar();
void drawDeviceListMenu();

#endif