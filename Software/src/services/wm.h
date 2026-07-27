#ifndef LOCKBOX_WM_H
#define LOCKBOX_WM_H

#include "WiFiManager.h"

extern WiFiManager wm;
extern SemaphoreHandle_t wmMutex;

void initWM();

#endif  // LOCKBOX_WM_H
