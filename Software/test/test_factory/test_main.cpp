#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <unity.h>

#include "devices/buttplugio/utils.h"

static bool fsInitialized = false;

static void initFilesystem() {
    if (fsInitialized) return;
    fsInitialized = LittleFS.begin(true);
}

void setUp(void) {}
void tearDown(void) {}

// --- Registry Mapping Tests ---

void test_registry_maps_lovense_uuid_to_lovense_config() {
    JsonDocument registryDoc;
    TEST_ASSERT_TRUE_MESSAGE(
        readJsonFile("/registry.json", registryDoc),
        "Failed to read /registry.json from LittleFS");

    const char* lovenseOnlyUuid = "48300001-0023-4bd4-bbd5-a6920e4c5653";
    TEST_ASSERT_FALSE_MESSAGE(
        registryDoc[lovenseOnlyUuid].isNull(),
        "Lovense-only UUID should exist in registry.json");

    JsonArrayConst configFiles = registryDoc[lovenseOnlyUuid];
    TEST_ASSERT_FALSE_MESSAGE(configFiles.isNull(),
                              "Config files array should not be null");

    bool foundLovense = false;
    for (JsonString file : configFiles) {
        if (String(file.c_str()) == "/protocols/lovense.json") {
            foundLovense = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(
        foundLovense,
        "Lovense-only UUID should map to /protocols/lovense.json");
}

void test_registry_maps_shared_uuid_includes_lovense() {
    JsonDocument registryDoc;
    TEST_ASSERT_TRUE(readJsonFile("/registry.json", registryDoc));

    const char* sharedUuid = "0000fff0-0000-1000-8000-00805f9b34fb";
    JsonArrayConst configFiles = registryDoc[sharedUuid];
    TEST_ASSERT_FALSE_MESSAGE(configFiles.isNull(),
                              "Shared UUID should exist in registry");

    bool foundLovense = false;
    int totalFiles = 0;
    for (JsonString file : configFiles) {
        totalFiles++;
        if (String(file.c_str()) == "/protocols/lovense.json") {
            foundLovense = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(totalFiles > 1,
                             "Shared UUID should map to multiple configs");
    TEST_ASSERT_TRUE_MESSAGE(
        foundLovense,
        "Shared UUID should include /protocols/lovense.json");
}

// --- Config Validation Tests ---

void test_lovense_config_validates() {
    JsonDocument configDoc;
    TEST_ASSERT_TRUE_MESSAGE(
        readJsonFile("/protocols/lovense.json", configDoc),
        "Failed to read /protocols/lovense.json from LittleFS");

    TEST_ASSERT_TRUE_MESSAGE(
        validateConfigStructure(configDoc, "/protocols/lovense.json"),
        "lovense.json should pass structure validation");
}

void test_non_lovense_config_also_validates() {
    JsonDocument configDoc;
    TEST_ASSERT_TRUE_MESSAGE(
        readJsonFile("/protocols/wevibe.json", configDoc),
        "Failed to read /protocols/wevibe.json from LittleFS");

    TEST_ASSERT_TRUE_MESSAGE(
        validateConfigStructure(configDoc, "/protocols/wevibe.json"),
        "wevibe.json should also pass structure validation");
}

// --- Name Pattern Matching Tests ---

void test_lovense_name_pattern_matches_lvs() {
    JsonDocument configDoc;
    TEST_ASSERT_TRUE(readJsonFile("/protocols/lovense.json", configDoc));

    JsonArrayConst names = configDoc["communication"][0]["btle"]["names"];
    TEST_ASSERT_FALSE_MESSAGE(names.isNull(),
                              "Lovense config should have name patterns");

    TEST_ASSERT_TRUE_MESSAGE(
        matchesDeviceName("LVS-Domi01", names),
        "LVS-Domi01 should match Lovense name patterns");
}

void test_lovense_name_pattern_matches_love() {
    JsonDocument configDoc;
    TEST_ASSERT_TRUE(readJsonFile("/protocols/lovense.json", configDoc));

    JsonArrayConst names = configDoc["communication"][0]["btle"]["names"];

    TEST_ASSERT_TRUE_MESSAGE(
        matchesDeviceName("LOVE-Test", names),
        "LOVE-Test should match Lovense name patterns");
}

void test_non_lovense_name_does_not_match() {
    JsonDocument configDoc;
    TEST_ASSERT_TRUE(readJsonFile("/protocols/lovense.json", configDoc));

    JsonArrayConst names = configDoc["communication"][0]["btle"]["names"];

    TEST_ASSERT_FALSE_MESSAGE(
        matchesDeviceName("WeVibe-Something", names),
        "WeVibe device name should NOT match Lovense patterns");

    TEST_ASSERT_FALSE_MESSAGE(
        matchesDeviceName("OSSM-Device", names),
        "OSSM device name should NOT match Lovense patterns");
}

// --- Characteristics Extraction Tests ---

void test_lovense_characteristics_extracted() {
    JsonDocument configDoc;
    TEST_ASSERT_TRUE(readJsonFile("/protocols/lovense.json", configDoc));

    std::string serviceUuid = "48300001-0023-4bd4-bbd5-a6920e4c5653";
    JsonObjectConst chars = extractCharacteristics(configDoc, serviceUuid);

    TEST_ASSERT_FALSE_MESSAGE(chars.isNull(),
                              "Should extract characteristics for Lovense UUID");
    TEST_ASSERT_FALSE_MESSAGE(chars["tx"].isNull(),
                              "Lovense characteristics should have tx UUID");
    TEST_ASSERT_FALSE_MESSAGE(chars["rx"].isNull(),
                              "Lovense characteristics should have rx UUID");
}

void test_invalid_uuid_returns_null_characteristics() {
    JsonDocument configDoc;
    TEST_ASSERT_TRUE(readJsonFile("/protocols/lovense.json", configDoc));

    std::string bogusUuid = "00000000-0000-0000-0000-000000000000";
    JsonObjectConst chars = extractCharacteristics(configDoc, bogusUuid);

    TEST_ASSERT_TRUE_MESSAGE(
        chars.isNull(),
        "Bogus UUID should return null characteristics");
}

// --- Factory Routing Logic Tests ---
// These test the core decision in buttplugIOFactory.cpp lines 79-84:
//   if (configFileName == "/protocols/lovense.json" ||
//       configFileName == "/protocols/lovense-connect-service.json")
//       → LovenseGeneric
//   else → GenericButtplugIODevice

static bool isLovenseRoute(const String& configFileName) {
    return configFileName == "/protocols/lovense.json" ||
           configFileName == "/protocols/lovense-connect-service.json";
}

void test_factory_routes_lovense_config() {
    TEST_ASSERT_TRUE_MESSAGE(
        isLovenseRoute("/protocols/lovense.json"),
        "lovense.json should route to LovenseGeneric");
}

void test_factory_routes_lovense_connect_config() {
    TEST_ASSERT_TRUE_MESSAGE(
        isLovenseRoute("/protocols/lovense-connect-service.json"),
        "lovense-connect-service.json should route to LovenseGeneric");
}

void test_factory_routes_generic_config() {
    TEST_ASSERT_FALSE_MESSAGE(
        isLovenseRoute("/protocols/wevibe.json"),
        "wevibe.json should NOT route to LovenseGeneric");

    TEST_ASSERT_FALSE_MESSAGE(
        isLovenseRoute("/protocols/satisfyer.json"),
        "satisfyer.json should NOT route to LovenseGeneric");
}

// --- End-to-End: Full Lovense Detection Path ---

void test_full_lovense_detection_path() {
    // Simulates the full factory path for a Lovense device:
    // 1. Service UUID found in registry → maps to lovense.json
    // 2. Config validates
    // 3. Device name matches Lovense pattern
    // 4. Characteristics extracted
    // 5. Config filename triggers LovenseGeneric route

    JsonDocument registryDoc;
    TEST_ASSERT_TRUE(readJsonFile("/registry.json", registryDoc));

    const char* uuid = "48300001-0023-4bd4-bbd5-a6920e4c5653";
    JsonArrayConst configFiles = registryDoc[uuid];
    TEST_ASSERT_FALSE(configFiles.isNull());

    String configFileName = configFiles[0].as<String>();
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "/protocols/lovense.json", configFileName.c_str(),
        "First config for Lovense UUID should be lovense.json");

    JsonDocument configDoc;
    TEST_ASSERT_TRUE(readJsonFile(configFileName, configDoc));
    TEST_ASSERT_TRUE(validateConfigStructure(configDoc, configFileName));

    JsonArrayConst names = configDoc["communication"][0]["btle"]["names"];
    TEST_ASSERT_TRUE_MESSAGE(
        matchesDeviceName("LVS-Solace01", names),
        "LVS-Solace01 should match");

    JsonObjectConst chars = extractCharacteristics(
        configDoc, std::string(uuid));
    TEST_ASSERT_FALSE(chars.isNull());

    TEST_ASSERT_TRUE_MESSAGE(
        isLovenseRoute(configFileName),
        "End-to-end: should route to LovenseGeneric");
}

void setup() {
    delay(2000);
    Serial.begin(115200);

    initFilesystem();
    TEST_ASSERT_TRUE_MESSAGE(fsInitialized,
                             "LittleFS must be mounted. Flash data/ with: "
                             "pio run -e development -t uploadfs");

    UNITY_BEGIN();

    RUN_TEST(test_registry_maps_lovense_uuid_to_lovense_config);
    RUN_TEST(test_registry_maps_shared_uuid_includes_lovense);

    RUN_TEST(test_lovense_config_validates);
    RUN_TEST(test_non_lovense_config_also_validates);

    RUN_TEST(test_lovense_name_pattern_matches_lvs);
    RUN_TEST(test_lovense_name_pattern_matches_love);
    RUN_TEST(test_non_lovense_name_does_not_match);

    RUN_TEST(test_lovense_characteristics_extracted);
    RUN_TEST(test_invalid_uuid_returns_null_characteristics);

    RUN_TEST(test_factory_routes_lovense_config);
    RUN_TEST(test_factory_routes_lovense_connect_config);
    RUN_TEST(test_factory_routes_generic_config);

    RUN_TEST(test_full_lovense_detection_path);

    UNITY_END();
}

void loop() {}
