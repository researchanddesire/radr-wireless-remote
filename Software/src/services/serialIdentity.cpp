#include "serialIdentity.h"
#if defined(ARDUINO) && !defined(PIO_UNIT_TESTING)
#include <Arduino.h>
#include <atomic>
#include "constants/Version.h"
#include "serialIdentityProtocol.h"
#include <serialIdentityBuild.h>
#if ARDUINO_USB_CDC_ON_BOOT && !ARDUINO_USB_MODE
#include <USB.h>
#endif
#ifndef FIRMWARE_BUILD_SHA
#define FIRMWARE_BUILD_SHA "unknown"
#endif
#ifndef FIRMWARE_TRACK
#define FIRMWARE_TRACK "development"
#endif

namespace {
TaskHandle_t identityTask = nullptr;
#ifdef VERSIONDEV
std::atomic<void (*)(const char*)> developmentCommand{nullptr};
std::atomic<void (*)()> developmentPoll{nullptr};
#endif

void announce() {
    const uint64_t mac = ESP.getEfuseMac();
    char deviceId[13];
    snprintf(deviceId, sizeof(deviceId), "%02X%02X%02X%02X%02X%02X",
             unsigned(mac & 255), unsigned((mac >> 8) & 255), unsigned((mac >> 16) & 255),
             unsigned((mac >> 24) & 255), unsigned((mac >> 32) & 255), unsigned((mac >> 40) & 255));
    const serialIdentity::Info info{RAD_ID_PRODUCT, RAD_ID_MODEL, VERSION, FIRMWARE_TRACK,
        FIRMWARE_BUILD_SHA, deviceId, RAD_ID_FLASH_BYTES, RAD_ID_PSRAM_BYTES,
        ESP.getFlashChipSize(), ESP.getPsramSize()};
    char frame[640];
    const size_t size = serialIdentity::format(frame, sizeof(frame), info);
    if (size) Serial.write(reinterpret_cast<const uint8_t*>(frame), size);
}

void console(void*) {
    // Never wait for a USB host or block application setup on a serial cable.
    vTaskDelay(pdMS_TO_TICKS(1000));
    announce();
    uint32_t lastAnnouncement = millis();
    serialIdentity::CommandBuffer parser;
    for (;;) {
        // Bound serial flood work so application tasks still get scheduled.
        for (int count = 0; count < 128 && Serial.available(); ++count) {
            const char* command = parser.push(Serial.read());
            if (!command) continue;
            if (strcmp(command, "info") == 0 || strcmp(command, "identity") == 0) {
                if (millis() - lastAnnouncement >= 250) {
                    announce();
                    lastAnnouncement = millis();
                }
            }
#ifdef VERSIONDEV
            else if (const auto handler = developmentCommand.load()) handler(command);
#endif
        }
#ifdef VERSIONDEV
        if (const auto poll = developmentPoll.load()) poll();
#endif
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
}

void configureSerialIdentityUsb() {
#if ARDUINO_USB_CDC_ON_BOOT && !ARDUINO_USB_MODE
    // Only TinyUSB descriptors are programmable. Keep VID/PID, serial number,
    // and the existing USB transport unchanged for reconnects and flashing.
    USB.productName(RAD_ID_MODEL);
    USB.manufacturerName("Research + Desire");
#elif ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE && !defined(VERSIONDEV) && !defined(RAD_HIL_VARIANT)
    // Production HWCDC also needs room for the complete identity record.
    Serial.setTxBufferSize(1024);
    Serial.setTxTimeoutMs(1000);
#endif
}

void startSerialIdentity() {
    if (identityTask) return;
#ifdef VERSIONDEV
    constexpr uint32_t stackBytes = 6144; // Includes development screen encoding.
#else
    constexpr uint32_t stackBytes = 3072;
#endif
    if (xTaskCreate(console, "serialIdentity", stackBytes, nullptr, 1, &identityTask) != pdPASS)
        Serial.println("RAD_ID_ERROR console_task");
}

#ifdef VERSIONDEV
void setSerialDevelopmentHandler(void (*command)(const char*), void (*poll)()) {
    developmentPoll.store(poll);
    developmentCommand.store(command);
}
#endif
#else
void configureSerialIdentityUsb() {}
void startSerialIdentity() {}
#ifdef VERSIONDEV
void setSerialDevelopmentHandler(void (*)(const char*), void (*)()) {}
#endif
#endif
