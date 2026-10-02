#include "devices/researchAndDesire/ossm/ossm_state.h"
#include "events.hpp"
#include "services/encoder.h"
#include "tasks/update.h"

// template <typename Event>
// constexpr bool is_valid(const Event &event)
// {
//     return event.valid;
// }

// template <typename Event>
//
template <typename Event>
const auto is_valid = [](const Event &event) {
    ESP_LOGI("TEST", "is_valid");
    return true;
};

template <typename Event = done>
const auto hasFilesystemUpdate =
    [](const Event &event) { return isFilesystemUpdateAvailable; };

template <typename Event = done>
const auto hasSoftwareUpdate =
    [](const Event &event) { return isSoftwareUpdateAvailable; };

// The last self-update step stopped for a reason (see updateFailureReason).
template <typename Event = done>
const auto updateFailed =
    [](const Event &event) { return !updateFailureReason.isEmpty(); };

template <typename Event = right_button_pressed>
const auto isOnline =
    [](const Event &event) { return WiFi.status() == WL_CONNECTED; };

template <typename Event = right_button_pressed>
auto isOption = [](MenuItemE value) {
    return [value](const Event &event) -> bool {
        auto currentOption = rightEncoder.readEncoder();
        auto indexOfValue = -1;

        for (int i = 0; i < activeMenuCount; i++) {
            if (activeMenu->at(i).id == value) {
                indexOfValue = i;
                break;
            }
        }

        bool result = currentOption == indexOfValue;
        return result;
    };
};

template <typename Event = right_button_pressed>
auto hasDeviceMenu = [](const Event &event) -> bool {
    return device != nullptr && device->menu.size() > 0;
};
const auto hasDeviceSettingsMenu = []() -> bool {
    return device != nullptr && device->settingsMenu.size() > 0;
};

const auto isPaused = []() -> bool {
    return device != nullptr && device->isPaused;
};

const auto isConnected = []() -> bool {
    return device != nullptr && device->isConnected;
};

const auto isSimplePenetrationMode = []() -> bool {
    return device != nullptr && device->isInSimplePenetrationMode();
};

// --- OSSM network jobs (pairing / update run on the OSSM, observed over BLE)

// The OSSM is in exactly this state (SML leaf name, e.g. "update.idle").
template <typename Event = ossm_state_event>
auto ossmInState = [](const char *state) {
    return [state](const Event &event) -> bool {
        const OssmObservedState observed = getOssmObservedState();
        return observed.valid && observed.info.state == state;
    };
};

// Pairing finished: the OSSM is in its pairing flow and reports isPaired.
template <typename Event = ossm_state_event>
const auto ossmPairingDone = [](const Event &event) -> bool {
    const OssmObservedState observed = getOssmObservedState();
    return observed.valid && observed.info.isPaired &&
           ossmStateStartsWith(observed.info.state, "pairing");
};

// The OSSM gave up pairing and reported why.
template <typename Event = ossm_state_event>
const auto ossmPairingFailed = [](const Event &event) -> bool {
    const OssmObservedState observed = getOssmObservedState();
    return observed.valid && observed.info.state == "pairing.failed";
};

// The OSSM has a claim code to show.
template <typename Event = ossm_state_event>
const auto ossmPairingCodeReady = [](const Event &event) -> bool {
    const OssmObservedState observed = getOssmObservedState();
    return observed.valid && !observed.info.pairingCode.empty() &&
           ossmStateStartsWith(observed.info.state, "pairing");
};
