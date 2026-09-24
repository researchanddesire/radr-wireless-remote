#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ossm_state_parser.hpp"

// Latest state read from the connected OSSM's state characteristic while a
// pairing or update page is open. The page task polls the characteristic
// (long read, so the MTU does not truncate the JSON; notifications arrive in
// 20-byte fragments on older OSSM firmware) and hands each reading here.
// Guards on the state machine read copies taken under a spinlock.
struct OssmObservedState {
    OssmStateInfo info;
    uint32_t updatedAtMs = 0;
    bool valid = false;
};

OssmObservedState getOssmObservedState();

// Forget the last observation and start a new follow generation, so a new
// network job on the OSSM cannot be judged by stale fields from the previous
// one and any older follow loop stops.
void resetOssmObservedState();

// Current follow generation; a follow loop exits when it changes.
uint32_t ossmFollowGeneration();

// Ends the current follow loop (called when leaving the OSSM network pages).
void stopOssmFollow();

// Stores a fresh reading and posts ossm_state_event when anything the state
// machine cares about changed. Returns false when the JSON is unparseable.
bool ingestOssmStateJson(const char *data, size_t length);
