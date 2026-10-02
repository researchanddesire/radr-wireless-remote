#pragma once
#include <stdlib.h>
struct base_event {
    virtual ~base_event() = default;
    int id{};
};

struct ack : public base_event {
    bool valid{};
};

struct fin : public base_event {
    bool valid{};
};

struct release : public base_event {};

struct timeout : public base_event {};

struct left_shoulder_pressed : public base_event {};

struct right_shoulder_pressed : public base_event {};

struct left_button_pressed : public base_event {};

struct wifi_connected : public base_event {};

struct right_button_pressed : public base_event {};

struct middle_button_pressed : public base_event {};

struct middle_button_long_pressed : public base_event {};

struct done : public base_event {};

struct connected_event : public base_event {};

struct connected_error_event : public base_event {};

struct disconnected_event : public base_event {};

struct left_encoder_changed : public base_event {
    int value{};
};

struct wake_up_event : public base_event {};

struct devices_found_event : public base_event {};

struct device_selected_event : public base_event {};

// The connected OSSM pushed a new state over BLE (see ossm_state.h).
struct ossm_state_event : public base_event {};

// The OSSM reported over BLE that it has no Wi-Fi, so it cannot run the
// requested network job (pairing / update).
struct ossm_no_wifi_event : public base_event {};

// The OSSM did not react to a BLE-triggered network job: its firmware
// predates go:pairing / go:update.
struct ossm_unsupported_event : public base_event {};

// A task the state machine needs could not be created (out of memory).
struct task_failed_event : public base_event {};
