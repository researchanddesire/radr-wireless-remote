#include "services/serialIdentity.h"
#include <Arduino.h>
#include "services/radHil.h"

#include <OneButton.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "components/AnimatedIcons.h"
#include "constants.h"
#include "esp_log.h"
#include <esp_heap_caps.h>
#include "pages/genericPages.h"
#include "pins.h"
#include "services/battery.h"
#include "services/buzzer.h"
#include "services/coms.h"
#include "services/display.h"
#include "services/encoder.h"
#include "services/imu.h"
#include "services/lastInteraction.h"
#include "services/leds.h"
#include "services/memory.h"
#include "services/vibrator.h"
#include "services/wm.h"
#include "state/remote.h"
#include "tasks/update.h"

#if !defined(CONFIG_ESP_IPC_TASK_STACK_SIZE) || \
    CONFIG_ESP_IPC_TASK_STACK_SIZE < 2048
#error "RADR requires an ESP-IDF IPC task stack of at least 2048 bytes"
#endif

OneButton leftShoulderBtn;
OneButton rightShoulderBtn;
OneButton underLeftBtn;
OneButton underCenterBtn;
OneButton underRightBtn;

#ifdef CONFIG_APP_ROLLBACK_ENABLE
// Let setup finish its device-level health checks before accepting a new OTA
// image. The Arduino core otherwise marks it valid before setup runs.
extern "C" bool verifyRollbackLater() { return true; }
#endif

// Largest free internal-RAM block the RADR must still have once BLE is up.
// Below this, internal-stack tasks (BLE connection, OTA) start failing to
// spawn. The number comes from hardware measurements (RAD-2158); a release
// that trips it must not be promoted. Override with -D RADR_MIN_LARGEST_BLOCK.
#ifndef RADR_MIN_LARGEST_BLOCK
#define RADR_MIN_LARGEST_BLOCK (24 * 1024)
#endif

static void logBootMemoryBudget() {
    const size_t freeInternal =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largestInternal =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    // Error level so it shows in production logs and on the desk before a
    // release is promoted.
    ESP_LOGE("MEM", "boot after BLE init: internal free=%u largest=%u budget=%u %s",
             (unsigned)freeInternal, (unsigned)largestInternal,
             (unsigned)RADR_MIN_LARGEST_BLOCK,
             largestInternal < RADR_MIN_LARGEST_BLOCK ? "LOW MEMORY" : "ok");
    if (largestInternal < RADR_MIN_LARGEST_BLOCK) {
        updateStatusText("LOW MEMORY: " + String((unsigned)(largestInternal / 1024)) +
                         " KB largest block");
    }
}

void setup() {
    configureSerialIdentityUsb();
    Serial.begin(115200);
    startSerialIdentity();

#ifdef DEBUG
    delay(5000);
#endif

    // Version 1.x of the PCB Boards cannot use PSRAM
    if (psramInit()) {
        ESP_LOGI(TAG, "PSRAM initialized");
    } else {
        ESP_LOGI(TAG, "PSRAM initialization failed");
    }

    ESP_LOGD(TAG, "PSRAM found: %d", psramFound());
    radHilStart();

    // init buttons
    leftShoulderBtn = OneButton(pins::BTN_L_SHOULDER, true, true);
    leftShoulderBtn.attachClick([]() {
        setNotIdle("left_shoulder_btn");
        stateMachine->process_event(left_shoulder_pressed());
    });
    rightShoulderBtn = OneButton(pins::BTN_R_SHOULDER, true, true);
    rightShoulderBtn.attachClick([]() {
        setNotIdle("right_shoulder_btn");
        stateMachine->process_event(right_shoulder_pressed());
    });
    underLeftBtn = OneButton(pins::BTN_UNDER_L, true, true);
    underLeftBtn.attachClick([]() {
        setNotIdle("under_left_btn");
        stateMachine->process_event(left_button_pressed());
    });
    underCenterBtn = OneButton(pins::BTN_UNDER_C, true, true);
    underCenterBtn.attachClick([]() {
        setNotIdle("under_center_btn");
        stateMachine->process_event(middle_button_pressed());
    });
    underCenterBtn.attachLongPressStart([]() {
        setNotIdle("under_center_btn");
        stateMachine->process_event(middle_button_long_pressed());
    });
    underRightBtn = OneButton(pins::BTN_UNDER_R, true, true);
    underRightBtn.attachClick([]() {
        setNotIdle("under_right_btn");
        stateMachine->process_event(right_button_pressed());
    });

    initMemoryService();
    initRegistry();
    initEncoderService();
    initFastLEDs();
    initWM();
    initDisplay();
    initBuzzer();
    initVibrator();
    initIMUService();
    updateIMUReadings();
    initBLE();
    logBootMemoryBudget();
    initStateMachine();
    initBattery();
    confirmRunningFirmware();

    setupAnimatedIcons();
    setupIdleMonitor();

    const BaseType_t buttonTaskCreated = xTaskCreatePinnedToCore(
        [](void *pvParameters) {
            while (true) {
                vTaskDelay(10);
                leftShoulderBtn.tick();
                rightShoulderBtn.tick();
                underLeftBtn.tick();
                underCenterBtn.tick();
                underRightBtn.tick();
                if (wmMutex != nullptr &&
                    xSemaphoreTake(wmMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    wm.process();
                    xSemaphoreGive(wmMutex);
                }
            }
        },
        "buttonTask", 6 * configMINIMAL_STACK_SIZE, NULL,
        configMAX_PRIORITIES - 1, NULL, 0);
    if (buttonTaskCreated != pdPASS) {
        ESP_LOGE(TAG, "Could not create button task; buttons will not respond");
    }
}

void loop() {
    // delete the loop task. Everything is managed by the state machine now.
    vTaskDelete(NULL);
}
