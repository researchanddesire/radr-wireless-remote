#include "wm.h"

#include "WiFi.h"
#include "devices/device.h"
#include "state/remote.h"

WiFiManager wm;
SemaphoreHandle_t wmMutex = nullptr;

void initWM() {
    if (wmMutex == nullptr) wmMutex = xSemaphoreCreateMutex();
    // Dynamic buffers: static Wi-Fi buffers pin ~23 KB of internal RAM at boot
    // that BLE and the UI tasks need. See PSRAM task notes in utils/psramTask.h.
    WiFi.useStaticBuffers(false);
    WiFi.begin();

    wm.setSaveConfigCallback(
        []() { stateMachine->process_event(wifi_connected()); });

    // When RADR gets an IP, share WiFi credentials with paired OSSM (if any)
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        xTimerPendFunctionCall([](void*, uint32_t) {
            if (device != nullptr && device->isConnected) {
                device->onWiFiConnected();
            }
        }, nullptr, 0, pdMS_TO_TICKS(100));
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
}
