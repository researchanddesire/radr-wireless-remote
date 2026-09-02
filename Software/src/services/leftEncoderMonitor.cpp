#include "leftEncoderMonitor.h"
#include "utils/psramTask.h"

#include "devices/device.h"
#include "encoder.h"
#include "lastInteraction.h"
#include "state/remote.h"

extern Device *device;

static TaskHandle_t leftEncoderTaskHandle = nullptr;

void leftEncoderMonitorTask(void *pvParameters) {
    leftEncoder.setEncoderValue(0);
    int lastLeftEncoderValue = leftEncoder.readEncoder();  // Initialize to zero
    ESP_LOGI("LeftEncoderMonitor",
             "Starting left encoder monitor task, initial value: %d",
             lastLeftEncoderValue);

    // Function to check if we're in device states
    auto isInDeviceStates = []() -> bool {
        using namespace sml;
        bool inControl = stateMachine->is("device_draw_control"_s);
        bool inMenu = stateMachine->is("device_menu"_s);
        bool inSimplePenetration = stateMachine->is("simple_penetration_control"_s);
        return inControl || inMenu || inSimplePenetration;
    };

    while (isInDeviceStates()) {
        int currentLeftEncoderValue = leftEncoder.readEncoder();

        if (currentLeftEncoderValue != lastLeftEncoderValue ||
            hasLeftEncoderChanged(false)) {
            setNotIdle("left_encoder");
            // Send encoder change to device
            if (device != nullptr) {
                device->onLeftEncoderChange(currentLeftEncoderValue);
            } else {
                ESP_LOGW("LeftEncoderMonitor",
                         "Device is null, cannot send encoder change");
            }

            lastLeftEncoderValue = currentLeftEncoderValue;
        }

        vTaskDelay(16 / portTICK_PERIOD_MS);  // ~60fps
    }

    ESP_LOGI("LeftEncoderMonitor", "Exiting left encoder monitor task");

    // Clean up task handle when exiting
    leftEncoderTaskHandle = nullptr;
    exitPsramTask();
}

void startLeftEncoderMonitoring() {
    // Kill existing task if it exists
    if (leftEncoderTaskHandle != nullptr) {
        deletePsramTask(leftEncoderTaskHandle);
        leftEncoderTaskHandle = nullptr;
    }

    createPsramTask(leftEncoderMonitorTask, "leftEncoderMonitor",
                    4 * configMINIMAL_STACK_SIZE, nullptr, 5,
                    &leftEncoderTaskHandle, 1);
}

void stopLeftEncoderMonitoring() {
    if (leftEncoderTaskHandle != nullptr) {
        deletePsramTask(leftEncoderTaskHandle);
        leftEncoderTaskHandle = nullptr;
    }
}