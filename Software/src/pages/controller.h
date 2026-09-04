#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"

class Device;

void drawControllerTask(void *pvParameters);

// Starts the controller UI task for `device`, first stopping any menu task and
// any previous controller task so only one task ever owns
// device->displayObjects at a time.
void startControllerTask(Device *device);

// Cooperatively stops the controller UI task (if running) and waits for it.
void stopControllerTask();

#endif