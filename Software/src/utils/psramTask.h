#ifndef RADR_PSRAM_TASK_H
#define RADR_PSRAM_TASK_H
#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Creates a task whose stack lives in PSRAM instead of internal RAM, falling
// back to a normal internal-RAM task when PSRAM is unavailable.
//
// Internal RAM on RADR is nearly exhausted by the BLE and Wi-Fi stacks, and
// every FreeRTOS task stack normally comes out of it. Tasks that only draw the
// display, drive LEDs, or poll inputs can safely run from a PSRAM stack.
//
// Fallback: the production build targets the N16R8 module (octal PSRAM). On an
// N16R2 module (quad PSRAM) that build cannot initialise PSRAM, so psramFound()
// is false and the task is created on an internal stack exactly as before.
//
// Rules for tasks created with this helper:
//   * The task must never perform flash operations itself (NVS, Preferences,
//     LittleFS writes, OTA). The cache is disabled during flash writes and a
//     PSRAM stack becomes unreachable.
//   * Delete with deletePsramTask() (from another task) or exitPsramTask()
//     (from itself), never plain vTaskDelete(). Both pick the correct delete
//     call for how the task was actually created.
inline BaseType_t createPsramTask(TaskFunction_t task, const char *name,
                                  uint32_t stackBytes, void *param,
                                  UBaseType_t priority, TaskHandle_t *handle,
                                  BaseType_t core) {
    BaseType_t result = pdFAIL;
    if (psramFound()) {
        result = xTaskCreatePinnedToCoreWithCaps(
            task, name, stackBytes, param, priority, handle, core,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (result != pdPASS) {
        result = xTaskCreatePinnedToCore(task, name, stackBytes, param,
                                         priority, handle, core);
    }
    if (result != pdPASS) {
        ESP_LOGE("TASK", "Could not create task %s (%u byte stack)", name,
                 (unsigned)stackBytes);
        if (handle != nullptr) *handle = nullptr;
    }
    return result;
}

// True when the task's stack was allocated by xTaskCreate*WithCaps (which
// creates a statically-allocated task around caller-provided buffers).
inline bool isPsramTask(TaskHandle_t handle) {
    StackType_t *stack = nullptr;
    StaticTask_t *tcb = nullptr;
    return xTaskGetStaticBuffers(handle, &stack, &tcb) == pdTRUE;
}

inline void deletePsramTask(TaskHandle_t handle) {
    if (handle == nullptr) return;
    if (isPsramTask(handle)) {
        vTaskDeleteWithCaps(handle);
    } else {
        vTaskDelete(handle);
    }
}

[[noreturn]] inline void exitPsramTask() {
    if (isPsramTask(xTaskGetCurrentTaskHandle())) {
        vTaskDeleteWithCaps(NULL);
    } else {
        vTaskDelete(NULL);
    }
    // Neither delete returns for the calling task; satisfy [[noreturn]].
    for (;;) vTaskDelay(portMAX_DELAY);
}

#endif  // RADR_PSRAM_TASK_H
