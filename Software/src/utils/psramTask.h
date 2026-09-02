#ifndef RADR_PSRAM_TASK_H
#define RADR_PSRAM_TASK_H
#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Creates a task whose stack lives in PSRAM instead of internal RAM.
//
// Internal RAM on RADR is nearly exhausted by the BLE and Wi-Fi stacks, and
// every FreeRTOS task stack normally comes out of it. Tasks that only draw the
// display, drive LEDs, or poll inputs can safely run from a PSRAM stack.
//
// Rules for tasks created with this helper:
//   * The task must never perform flash operations itself (NVS, Preferences,
//     LittleFS writes, OTA). The cache is disabled during flash writes and a
//     PSRAM stack becomes unreachable.
//   * The task must be deleted with deletePsramTask() (from another task) or
//     exitPsramTask() (from itself), never plain vTaskDelete().
inline BaseType_t createPsramTask(TaskFunction_t task, const char *name,
                                  uint32_t stackBytes, void *param,
                                  UBaseType_t priority, TaskHandle_t *handle,
                                  BaseType_t core) {
    const BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
        task, name, stackBytes, param, priority, handle, core,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (result != pdPASS) {
        ESP_LOGE("TASK", "Could not create PSRAM task %s (%u byte stack)", name,
                 (unsigned)stackBytes);
        if (handle != nullptr) *handle = nullptr;
    }
    return result;
}

inline void deletePsramTask(TaskHandle_t handle) { vTaskDeleteWithCaps(handle); }

[[noreturn]] inline void exitPsramTask() {
    vTaskDeleteWithCaps(NULL);
    // vTaskDeleteWithCaps(NULL) does not return; satisfy [[noreturn]].
    for (;;) vTaskDelay(portMAX_DELAY);
}

#endif  // RADR_PSRAM_TASK_H
