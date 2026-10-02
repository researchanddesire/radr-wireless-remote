#pragma once

// Asks the connected OSSM to check for and install its own firmware update
// (BLE go:update). Progress arrives as ossm_state_event; see machine.h.
void startOssmUpdateRequest();

// Draws the Install / Cancel page with the version the OSSM was offered.
void drawOssmUpdateAvailableFromState();

// Second go:update = confirm the install on the OSSM.
void confirmOssmInstall();
