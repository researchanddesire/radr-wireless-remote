#include <Arduino.h>

#include <OneButton.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "components/AnimatedIcons.h"
#include "constants.h"
#include "esp_log.h"
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
// TEST COMMENT
#include "state/remote.h"

// Function declaration for resetting middle button counter
extern void resetMiddleButtonCounter();

OneButton leftShoulderBtn;
OneButton rightShoulderBtn;
OneButton underLeftBtn;
OneButton underCenterBtn;
OneButton underRightBtn;

// Global variables for middle button press tracking
static int middleButtonPressCount = 0;

// Function to reset middle button counter (can be called from device)
void resetMiddleButtonCounter() { middleButtonPressCount = 0; }

void setup() {
    Serial.begin(115200);

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
        middleButtonPressCount++;
        if (middleButtonPressCount == 1) {
            // First press action
            stateMachine->process_event(middle_button_pressed());
        } else if (middleButtonPressCount >= 2) {
            // Second press action
            resetMiddleButtonCounter();
            stateMachine->process_event(middle_button_second_press());
        }
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
    initBLE();
    initStateMachine();
    initBattery();

    // initIMUService();
    // updateIMUReadings();

    setupAnimatedIcons();
    setupIdleMonitor();

    // xTaskCreatePinnedToCore(
    //     [](void *pvParameters) {
    //         while (true) {
    //             vTaskDelay(10);
    //             leftShoulderBtn.tick();
    //             rightShoulderBtn.tick();
    //             underLeftBtn.tick();
    //             underCenterBtn.tick();
    //             underRightBtn.tick();
    //             wm.process();
    //         }
    //     },
    //     "buttonTask", 6 * configMINIMAL_STACK_SIZE, NULL,
    //     configMAX_PRIORITIES - 1, NULL, 0);
}

void loop() {
    // delete the loop task. Everything is managed by the state machine now.
    vTaskDelete(NULL);
}
