#ifndef GENERIC_PAGES_H
#define GENERIC_PAGES_H

#include "constants/Sizes.h"
#include "pages/TextPages.h"

void drawPageTask(void *pvParameters);
void updateStatusText(const String &statusMessage);  // Future plans to expand
                                                     // to include text color.

#endif