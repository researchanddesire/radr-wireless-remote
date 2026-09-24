#include "ossm_state.h"

#include <Arduino.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

// Defined in remote.cpp; avoids pulling the state-machine headers (and their
// header-static pages) into this translation unit.
void fireStateMachineOssmStateEvent();

namespace {

portMUX_TYPE observedLock = portMUX_INITIALIZER_UNLOCKED;
OssmObservedState observed;
uint32_t followGeneration = 0;
constexpr const char *OSSM_STATE_TAG = "OSSM_STATE";

void postStateEvent(void *, uint32_t) { fireStateMachineOssmStateEvent(); }

bool sameForStateMachine(const OssmStateInfo &a, const OssmStateInfo &b) {
    return a.state == b.state && a.error == b.error &&
           a.pairingCode == b.pairingCode && a.isPaired == b.isPaired &&
           a.targetVersion == b.targetVersion;
}

}  // namespace

OssmObservedState getOssmObservedState() {
    taskENTER_CRITICAL(&observedLock);
    OssmObservedState copy = observed;
    taskEXIT_CRITICAL(&observedLock);
    return copy;
}

void resetOssmObservedState() {
    taskENTER_CRITICAL(&observedLock);
    observed = OssmObservedState{};
    followGeneration++;
    taskEXIT_CRITICAL(&observedLock);
}

uint32_t ossmFollowGeneration() {
    taskENTER_CRITICAL(&observedLock);
    const uint32_t generation = followGeneration;
    taskEXIT_CRITICAL(&observedLock);
    return generation;
}

void stopOssmFollow() {
    taskENTER_CRITICAL(&observedLock);
    followGeneration++;
    taskEXIT_CRITICAL(&observedLock);
}

bool ingestOssmStateJson(const char *data, size_t length) {
    OssmStateInfo info;
    if (!parseOssmState(data, length, info)) {
        ESP_LOGW(OSSM_STATE_TAG, "Unparseable OSSM state (%u bytes): %.*s",
                 (unsigned)length, (int)(length > 400 ? 400 : length), data);
        return false;
    }

    taskENTER_CRITICAL(&observedLock);
    const bool changed = !observed.valid || !sameForStateMachine(observed.info, info);
    observed.info = info;
    observed.updatedAtMs = millis();
    observed.valid = true;
    taskEXIT_CRITICAL(&observedLock);

    if (changed) {
        ESP_LOGI(OSSM_STATE_TAG, "OSSM state=%s error=%s code=%s paired=%d",
                 info.state.c_str(), info.error.c_str(), info.pairingCode.c_str(),
                 info.isPaired ? 1 : 0);
        // Deferred to the timer task, same pattern as Device::onDisconnect, so
        // state machine actions never run on the polling task.
        xTimerPendFunctionCall(postStateEvent, nullptr, 0, pdMS_TO_TICKS(10));
    }
    return true;
}
