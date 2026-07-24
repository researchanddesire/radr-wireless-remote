#include <unity.h>

#include "FirmwareUpdateProtocol.h"

namespace {

const char *VALID_RESPONSE = R"json({
  "protocolVersion": 1,
  "shouldUpdate": true,
  "updateAvailable": true,
  "reason": "update-available",
  "reportedTrack": "main",
  "assignedTrack": "staging",
  "trackChanged": true,
  "currentVersion": "1.0.42",
  "targetVersion": "1.0.43",
  "nextHopVersion": "1.0.43",
  "update": {
    "releaseId": "00000000-0000-4000-8000-000000000001",
    "buildSha": "0123456789abcdef",
    "kind": "firmware",
    "publishedAt": "2026-07-16T00:00:00.000Z",
    "artifacts": [
      {
        "role": "application",
        "url": "https://example.supabase.co/storage/v1/object/public/radr-firmware/releases/1.0.43/0123456789abcdef/firmware.bin",
        "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "sizeBytes": 1024,
        "installOrder": 1
      }
    ]
  },
  "nextCheckSeconds": 60
})json";

void test_serializes_required_and_hardware_fields() {
    firmware::DeviceReport report{
        .deviceType = "radr",
        .deviceId = "trainer-test-id",
        .reportedTrack = "main",
        .currentVersion = "1.0.42",
        .currentBuild = "0123456789abcdef",
        .firmwareHash =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        .chip = "esp32",
        .chipRevision = 0,
        .chipCores = 2,
        .flashSizeBytes = 4194304,
        .otaSlotSizeBytes = 1310720,
        .psramSizeBytes = 0,
        .partitionLayout = "legacy-v1",
    };
    JsonDocument parsed;
    deserializeJson(parsed, firmware::serializeReport(report));
    TEST_ASSERT_EQUAL_STRING("radr", parsed["deviceType"]);
    TEST_ASSERT_EQUAL_INT(1, parsed["protocolVersion"]);
    TEST_ASSERT_EQUAL_STRING("main", parsed["reportedTrack"]);
    TEST_ASSERT_EQUAL_UINT32(4194304, parsed["flashSizeBytes"]);
    TEST_ASSERT_EQUAL_UINT32(1310720, parsed["otaSlotSizeBytes"]);
    TEST_ASSERT_EQUAL_UINT32(0, parsed["chipRevision"]);
    TEST_ASSERT_EQUAL_UINT32(2, parsed["chipCores"]);
    TEST_ASSERT_EQUAL_UINT32(0, parsed["psramSizeBytes"]);
    TEST_ASSERT_EQUAL_STRING("legacy-v1", parsed["partitionLayout"]);

    report.chipCores = 0;
    JsonDocument withoutCoreCount;
    deserializeJson(withoutCoreCount, firmware::serializeReport(report));
    TEST_ASSERT_TRUE(withoutCoreCount["chipCores"].isNull());
    TEST_ASSERT_EQUAL_UINT32(0, withoutCoreCount["chipRevision"]);
    TEST_ASSERT_EQUAL_UINT32(0, withoutCoreCount["psramSizeBytes"]);
}

void test_parses_cross_track_update_and_orders_artifacts() {
    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_TRUE(
        firmware::parseDecision(VALID_RESPONSE, "radr", decision, error));
    TEST_ASSERT_TRUE(decision.updateAvailable);
    TEST_ASSERT_TRUE(decision.shouldUpdate);
    TEST_ASSERT_EQUAL_STRING("update-available", decision.reason.c_str());
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef", decision.buildSha.c_str());
    TEST_ASSERT_EQUAL_STRING("staging", decision.assignedTrack.c_str());
    TEST_ASSERT_TRUE(decision.trackChanged);
    TEST_ASSERT_EQUAL_STRING("1.0.43", decision.nextHopVersion.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, decision.artifactCount);
    TEST_ASSERT_EQUAL_STRING("application", decision.artifacts[0].role.c_str());
}

void test_accepts_legacy_boolean_and_rejects_conflicting_decisions() {
    std::string legacy = VALID_RESPONSE;
    const std::string canonical = "  \"shouldUpdate\": true,\n";
    legacy.erase(legacy.find(canonical), canonical.size());
    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_TRUE(
        firmware::parseDecision(legacy, "radr", decision, error));
    TEST_ASSERT_TRUE(decision.shouldUpdate);

    std::string conflicting = VALID_RESPONSE;
    const auto shouldUpdate = conflicting.find("\"shouldUpdate\": true");
    conflicting.replace(shouldUpdate, std::string("\"shouldUpdate\": true").size(),
                        "\"shouldUpdate\": false");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(conflicting, "radr", decision, error));

    std::string incoherentReason = VALID_RESPONSE;
    const auto reason = incoherentReason.find("update-available");
    incoherentReason.replace(reason, std::string("update-available").size(),
                             "already-current");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(incoherentReason, "radr", decision, error));
}

void test_requires_build_sha_for_canonical_updates_only() {
    std::string missingBuild = VALID_RESPONSE;
    const std::string buildLine =
        "    \"buildSha\": \"0123456789abcdef\",\n";
    missingBuild.erase(missingBuild.find(buildLine), buildLine.size());

    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_FALSE(
        firmware::parseDecision(missingBuild, "radr", decision, error));
    TEST_ASSERT_EQUAL_STRING(
        "canonical update response is missing build SHA", error.c_str());

    std::string legacy = missingBuild;
    const std::string protocolLine = "  \"protocolVersion\": 1,\n";
    const std::string canonicalLine = "  \"shouldUpdate\": true,\n";
    const std::string reasonLine =
        "  \"reason\": \"update-available\",\n";
    legacy.erase(legacy.find(protocolLine), protocolLine.size());
    legacy.erase(legacy.find(canonicalLine), canonicalLine.size());
    legacy.erase(legacy.find(reasonLine), reasonLine.size());
    TEST_ASSERT_TRUE(firmware::parseDecision(legacy, "radr", decision, error));
    TEST_ASSERT_TRUE(decision.shouldUpdate);
    TEST_ASSERT_TRUE(decision.buildSha.empty());

    std::string invalidBuild = VALID_RESPONSE;
    const auto buildValue = invalidBuild.find("0123456789abcdef");
    invalidBuild.replace(buildValue, std::string("0123456789abcdef").size(),
                         "invalid");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(invalidBuild, "radr", decision, error));
    TEST_ASSERT_EQUAL_STRING("invalid update build identifier", error.c_str());
}

void test_rejects_repeat_build_on_same_track() {
    firmware::DeviceReport report{
        .deviceType = "radr",
        .deviceId = "aabbccddeeff",
        .reportedTrack = "main",
        .currentVersion = "1.0.42",
        .currentBuild = "0123456",
    };
    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_TRUE(
        firmware::parseDecision(VALID_RESPONSE, "radr", decision, error));
    decision.assignedTrack = "main";
    decision.trackChanged = false;
    TEST_ASSERT_FALSE(firmware::decisionMatchesReport(report, decision));

    decision.buildSha = "fedcba9";
    TEST_ASSERT_TRUE(firmware::decisionMatchesReport(report, decision));
    decision.assignedTrack = "staging";
    decision.trackChanged = true;
    decision.buildSha = "0123456789abcdef";
    TEST_ASSERT_TRUE(firmware::decisionMatchesReport(report, decision));
}

void test_rejects_wrong_bucket_and_non_https_urls() {
    std::string payload = VALID_RESPONSE;
    const auto bucket = payload.find("radr-firmware");
    payload.replace(bucket, std::string("radr-firmware").size(), "lkbx-firmware");
    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "radr", decision, error));

    payload = VALID_RESPONSE;
    const auto scheme = payload.find("https://");
    payload.replace(scheme, std::string("https://").size(), "http://");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "radr", decision, error));

    payload = VALID_RESPONSE;
    const auto trustedHost = payload.find("example.supabase.co");
    payload.replace(trustedHost, std::string("example.supabase.co").size(),
                    "evil.example");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "radr", decision, error));
}

void test_rejects_bad_hash_duplicate_order_and_missing_fields() {
    std::string payload = VALID_RESPONSE;
    const auto hash = payload.find(std::string(64, 'a'));
    payload.replace(hash, 64, "bad");
    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "radr", decision, error));
    TEST_ASSERT_FALSE(firmware::parseDecision("{}", "radr", decision, error));
}

}  // namespace

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_serializes_required_and_hardware_fields);
    RUN_TEST(test_parses_cross_track_update_and_orders_artifacts);
    RUN_TEST(test_accepts_legacy_boolean_and_rejects_conflicting_decisions);
    RUN_TEST(test_requires_build_sha_for_canonical_updates_only);
    RUN_TEST(test_rejects_repeat_build_on_same_track);
    RUN_TEST(test_rejects_wrong_bucket_and_non_https_urls);
    RUN_TEST(test_rejects_bad_hash_duplicate_order_and_missing_fields);
    return UNITY_END();
}
