#pragma once

#include "pages/TextPages.h"

// Asks the connected OSSM to pair itself with the RAD Dashboard (BLE
// go:pairing). Result arrives as ossm_state_event; see machine.h.
void startOssmPairingRequest();

// Draws the claim code + QR from the last observed OSSM state.
void drawOssmPairingCodeFromState();

// Draws `page` and, under it, the reason text for the OSSM's last error.
// reason == nullptr -> use the error code reported by the OSSM
void drawOssmFailurePage(const TextPage &page, const char *reason = nullptr);
