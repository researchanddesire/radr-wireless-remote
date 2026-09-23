#include "radHil.h"

#if defined(RAD_HIL_VARIANT) && defined(ARDUINO)
#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <time.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_timer.h>
#if __has_include("mqtt.h")
#include "mqtt.h"
#define RAD_HIL_LOCKBOX_MQTT
#endif
#if __has_include("CertificateDateClient.h")
#include "CertificateDateClient.h"
#include <HTTPClient.h>
#include <time.h>
#define RAD_HIL_CERTIFICATE_DATE_CLIENT
#ifndef RAD_HIL_ARDUINO_HTTP
#define RAD_HIL_ARDUINO_HTTP
#endif
#elif defined(RAD_HIL_ARDUINO_HTTP)
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#else
// Arduino 2 also ships a header with this name exposing only its wrapper.
// The IDF symbol is present in the pinned SDK's libmbedtls.a in both builds.
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);
#endif
#if ESP_IDF_VERSION_MAJOR >= 5
#include <esp_mac.h>
#endif

namespace {
std::atomic<uint32_t> progressMs{0};
std::atomic<bool> ready{false};
std::atomic<uint32_t> disconnects{0};
uint32_t bootId = 0;
uint32_t lastNetworkMs = 0;
bool internet = false;

unsigned long long uptimeMs() {
    return static_cast<unsigned long long>(esp_timer_get_time() / 1000);
}

bool checkInternet() {
    // TLS certificate dates require a synchronized clock. Some products do
    // not initialize SNTP during ordinary idle startup, so the staging-only
    // observer requests it once and waits without weakening verification.
    static bool clockRequested = false;
    if (time(nullptr) < 1704067200) {
        if (!clockRequested) {
            configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
            clockRequested = true;
        }
        return false;
    }
    // A read-only request with full certificate/hostname validation. Never
    // follow a redirect to a different environment or send device credentials.
#if defined(RAD_HIL_ARDUINO_HTTP)
    // Reuse the Trainer's existing HTTP stack to retain its small OTA slot.
    extern const uint8_t bundleStart[] asm("_binary_x509_crt_bundle_start");
#if defined(RAD_HIL_CERTIFICATE_DATE_CLIENT)
    // Preserve the public Trainer's SDK certificate-date hardening.
    firmware::CertificateDateClient client;
#else
    WiFiClientSecure client;
#endif
    client.setCACertBundle(bundleStart);
    client.setHandshakeTimeout(5);
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(client, "https://staging.researchanddesire.com/")) return false;
    const int status = http.sendRequest("HEAD");
    http.end();
    return status >= 200 && status < 400;
#else
    esp_http_client_config_t config = {};
    config.url = "https://staging.researchanddesire.com/";
    config.method = HTTP_METHOD_HEAD;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.timeout_ms = 5000;
    config.disable_auto_redirect = true;
    config.buffer_size = 512;
#if defined(FIRMWARE_USE_IDF_CRT_BUNDLE)
    config.crt_bundle_attach = esp_crt_bundle_attach;
#else
    config.crt_bundle_attach = arduino_esp_crt_bundle_attach;
#endif
    auto client = esp_http_client_init(&config);
    if (!client) return false;
    const auto result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return result == ESP_OK && status >= 200 && status < 400;
#endif
}

void observeTask(void*) {
    uint32_t previousProbe = 0;
    for (;;) {
        const uint32_t now = millis();
        const bool wifi = WiFi.status() == WL_CONNECTED;
        if (!wifi) internet = false;
        if (wifi && (previousProbe == 0 || now - previousProbe >= 30000)) {
            previousProbe = now;
            internet = checkInternet();
            if (internet) lastNetworkMs = millis();
        }
        const uint32_t sampled = millis();
        const auto lastProgress = progressMs.load(std::memory_order_relaxed);
        bool applicationReady = ready.load(std::memory_order_relaxed) && lastProgress != 0;
#if defined(RAD_HIL_LOCKBOX_MQTT)
        // Retain the original Lockbox gate's staging MQTT requirement.
        applicationReady = applicationReady && mqttConnected && mqtt_server != nullptr &&
            std::strcmp(mqtt_server, "mqtts://x15ff600.ala.us-east-1.emqxsl.com") == 0;
#endif
        std::printf(
            "RAD_HEALTH {\"event\":\"heartbeat\",\"boot_id\":%lu,\"uptime_ms\":%llu,"
            "\"ready\":%s,\"app_age_ms\":%lu,\"wifi\":%s,\"bench_wifi\":%s,"
            "\"ip\":\"%s\",\"internet\":%s,\"network_age_ms\":%lu,\"disconnects\":%lu}\n",
            static_cast<unsigned long>(bootId), uptimeMs(),
            applicationReady ? "true" : "false",
            static_cast<unsigned long>(sampled-lastProgress),
            WiFi.status() == WL_CONNECTED ? "true" : "false",
            WiFi.SSID() == "IoT_PHB" ? "true" : "false", WiFi.localIP().toString().c_str(),
            internet ? "true" : "false", static_cast<unsigned long>(sampled-lastNetworkMs),
            static_cast<unsigned long>(disconnects.load(std::memory_order_relaxed)));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
}  // namespace

void radHilStart() {
    if (bootId != 0) return;
    bootId = esp_random();
    if (bootId == 0) bootId = 1;
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    char deviceId[13];
    snprintf(deviceId, sizeof(deviceId), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    uint8_t hash[32] = {};
    const auto partition = esp_ota_get_running_partition();
    if (!partition || esp_partition_get_sha256(partition, hash) != ESP_OK) {
        std::printf( "RAD_HEALTH invalid running image\n");
        return;
    }
    char imageHash[65];
    for (size_t i = 0; i < sizeof(hash); ++i) snprintf(imageHash+i*2, 3, "%02x", hash[i]);
    std::printf(
        "RAD_HEALTH {\"event\":\"boot\",\"schema\":1,\"boot_id\":%lu,\"uptime_ms\":%llu,"
        "\"product\":\"%s\",\"variant\":\"%s\",\"device_id\":\"%s\",\"flash_bytes\":%lu,"
        "\"build_sha\":\"%s\",\"image_sha256\":\"%s\",\"track\":\"%s\"}\n",
        static_cast<unsigned long>(bootId), uptimeMs(), RAD_HIL_PRODUCT, RAD_HIL_VARIANT,
        deviceId, static_cast<unsigned long>(ESP.getFlashChipSize()), FIRMWARE_BUILD_SHA, imageHash, FIRMWARE_TRACK);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
        disconnects.fetch_add(1, std::memory_order_relaxed);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    if (xTaskCreate(observeTask, "radHil", 10240, nullptr, 1, nullptr) != pdPASS) {
        std::printf( "RAD_HEALTH observer task creation failed\n");
    }
}

void radHilProgress(bool idleAndReady) {
    ready.store(idleAndReady, std::memory_order_relaxed);
    progressMs.store(millis(), std::memory_order_relaxed);
}
#else
void radHilStart() {}
void radHilProgress(bool) {}
#endif
