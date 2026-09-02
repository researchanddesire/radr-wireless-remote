#ifndef LOCKBOX_LASTINTERACTION_H
#define LOCKBOX_LASTINTERACTION_H

#include <Arduino.h>
#include "utils/psramTask.h"
#include "display.h"
#include "esp_log.h"

#ifdef SHORT_TIMEOUTS
static const unsigned long SLEEP_TIMEOUT = 30000;        // 30 seconds
static const unsigned long PSEUDO_SLEEP_TIMEOUT = 10000; // 10 seconds
static const unsigned long IDLE_TIMEOUT = 5000;          // 5 seconds
#else
static const unsigned long SLEEP_TIMEOUT = 60000 * 10;       // 10 minutes
static const unsigned long PSEUDO_SLEEP_TIMEOUT = 60000 * 2; // 2 minutes
static const unsigned long IDLE_TIMEOUT = 30000;             // 30 seconds
#endif

enum class IdleState
{
    SLEEP,
    PSEUDO_SLEEP,
    IDLE,
    NOT_IDLE
};

// Declare as extern - actual storage is in lastInteraction.cpp
// volatile ensures compiler doesn't cache these across threads
extern volatile unsigned long sleepDuration;
extern volatile unsigned long lastInteraction;
extern volatile IdleState idleState;

inline void setNotIdle(String src)
{
    lastInteraction = millis();

    if (idleState == IdleState::NOT_IDLE)
    {
        return;  // Already active, just update timestamp
    }
    
    // We were idle/dimmed, now becoming active again
    restoreScreenBrightness();
    idleState = IdleState::NOT_IDLE;
    ESP_LOGI("IDLE", "Restored from idle, called from %s", src.c_str());
}

inline void setupIdleMonitor() {
    lastInteraction = millis();
    
    auto task = [](void *pvParameters) {
        while (true) {
            unsigned long elapsed = millis() - lastInteraction;
            
            // Only act on state TRANSITIONS, not every loop
            if (elapsed > SLEEP_TIMEOUT && idleState != IdleState::SLEEP) {
                idleState = IdleState::SLEEP;
                turnOffScreen();
                sleepDuration = elapsed;
                ESP_LOGI("IDLE", "Entering SLEEP state");
                // Could trigger deep sleep here in the future
            }
            else if (elapsed > PSEUDO_SLEEP_TIMEOUT && idleState == IdleState::IDLE) {
                idleState = IdleState::PSEUDO_SLEEP;
                ESP_LOGI("IDLE", "Entering PSEUDO_SLEEP state");
            }
            else if (elapsed > IDLE_TIMEOUT && idleState == IdleState::NOT_IDLE) {
                idleState = IdleState::IDLE;
                dimScreen();
                ESP_LOGI("IDLE", "Entering IDLE state - dimming screen");
            }
            
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    };

    createPsramTask(task, "idle_monitor", 4 * configMINIMAL_STACK_SIZE, nullptr,
                    tskIDLE_PRIORITY, nullptr, 0);
}

#endif // LOCKBOX_LASTINTERACTION_H
