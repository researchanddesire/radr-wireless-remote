#pragma once

#include <stddef.h>

// Tears the whole BLE stack down so the RADR can do TLS. Measured on hardware
// (RAD-2158, 2026-09-08): with an OSSM connected the largest free internal
// block is ~14 KB; after this call it is ~33 KB with ~127 KB free, and the
// firmware check over HTTPS succeeds. There is no way back except a restart,
// which every exit of the update flow already does.
//
// Order matters: the connected peripheral (and its client callbacks) go first,
// then scanning and advertising, then the RAD BLE server task, then NimBLE
// itself. NimBLE deinit(true) deletes every server/client object it created;
// callback objects registered with ownership must therefore be heap-owned or
// registered with deleteCallbacks=false (see initBLE in coms.cpp).
struct BleShutdownResult {
    size_t freeInternal = 0;
    size_t largestInternal = 0;
    bool deinitOk = false;
};

BleShutdownResult shutdownBleForNetwork();
