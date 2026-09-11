
#ifndef RADR_UPDATE_H
#define RADR_UPDATE_H
#include <Arduino.h>

#include <task.h>

extern bool isSoftwareUpdateAvailable;
extern bool isFilesystemUpdateAvailable;

// Why the last self-update step stopped. Empty means "no update available"
// (up to date); anything else is shown on the Update Failed page.
extern String updateFailureReason;

bool isUpdateAvailable();
bool confirmRunningFirmware();

// FREERTOS Task

extern TaskHandle_t updateTaskHandle;
constexpr char updateTaskName PROGMEM[] = "updateTask";
extern TaskHandle_t updateFilesystemTaskHandle;
constexpr char updateFilesystemTaskName PROGMEM[] = "updateFilesystemTask";
extern TaskHandle_t updateSoftwareTaskHandle;
constexpr char updateSoftwareTaskName PROGMEM[] = "updateSoftwareTask";

void updateTask(void *pvParameters);
void updateFilesystemTask(void *pvParameters);
void updateSoftwareTask(void *pvParameters);
#endif
