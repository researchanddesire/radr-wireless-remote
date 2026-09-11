#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include "remote.h"

StateLogger stateLogger;
// Static pointer to hold the state machine instance
sml::sm<ossm_remote_state, sml::thread_safe<ESP32RecursiveMutex>, sml::logger<StateLogger>> *stateMachine = nullptr;

void initStateMachine()
{
    if (stateMachine == nullptr)
    {
        stateMachine = new sml::sm<ossm_remote_state, sml::thread_safe<ESP32RecursiveMutex>, sml::logger<StateLogger>>(stateLogger);

        stateMachine->process_event(done{});
    }
}

void fireStateMachineDoneEvent() {
    if (stateMachine) {
        stateMachine->process_event(done{});
    }
}

// Deferred to the timer task: this is fired from inside on_entry actions
// (task creation happens there), and SML must not re-enter process_event.
static void postTaskFailedEvent(void *, uint32_t) {
    if (stateMachine) {
        stateMachine->process_event(task_failed_event{});
    }
}

void fireStateMachineTaskFailedEvent() {
    xTimerPendFunctionCall(postTaskFailedEvent, nullptr, 0, pdMS_TO_TICKS(10));
}

void fireStateMachineOssmStateEvent() {
    if (stateMachine) {
        stateMachine->process_event(ossm_state_event{});
    }
}

void fireStateMachineOssmNoWifiEvent() {
    if (stateMachine) {
        stateMachine->process_event(ossm_no_wifi_event{});
    }
}

void fireStateMachineOssmUnsupportedEvent() {
    if (stateMachine) {
        stateMachine->process_event(ossm_unsupported_event{});
    }
}

void fireStateMachineOssmLinkLostEvent() {
    if (stateMachine) {
        stateMachine->process_event(disconnected_event{});
    }
}
