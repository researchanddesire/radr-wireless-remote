#include "ble_lifecycle.h"

#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "devices/device.h"
#include "services/leftEncoderMonitor.h"
#include "services/rad_ble.h"

namespace {
constexpr const char *BLE_TAG = "BLE_LIFECYCLE";

void logInternal(const char *label) {
    ESP_LOGW(BLE_TAG, "[MEM] %s: internal free=%u largest=%u", label,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT));
}
}  // namespace

BleShutdownResult shutdownBleForNetwork() {
    BleShutdownResult result;
    logInternal("before BLE shutdown");

    if (device != nullptr) {
        if (device->needsPersistentLeftEncoderMonitoring()) {
            stopLeftEncoderMonitoring();
        }
        delete device;  // disconnects and clears the client callbacks
        device = nullptr;
    }

    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan != nullptr && scan->isScanning()) scan->stop();
    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    if (advertising != nullptr) advertising->stop();
    radBleServer.end();
    vTaskDelay(pdMS_TO_TICKS(100));
    logInternal("after peripheral/server stopped");

    result.deinitOk = NimBLEDevice::deinit(true);
    vTaskDelay(pdMS_TO_TICKS(300));
    result.freeInternal =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    result.largestInternal =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGW(BLE_TAG, "[MEM] after NimBLE deinit (%s): internal free=%u largest=%u",
             result.deinitOk ? "ok" : "FAILED", (unsigned)result.freeInternal,
             (unsigned)result.largestInternal);
    return result;
}
