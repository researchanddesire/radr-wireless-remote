#pragma once

// Call configure before Serial.begin, then start once the UART is available.
void configureSerialIdentityUsb();
void startSerialIdentity();
#ifdef VERSIONDEV
void setSerialDevelopmentHandler(void (*command)(const char*), void (*poll)());
#endif
