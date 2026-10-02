#include <unity.h>

#include <string>

#include "devices/researchAndDesire/ossm/ossm_state_parser.hpp"

static bool parse(const char *json, OssmStateInfo &out) {
    return parseOssmState(json, std::strlen(json), out);
}

void test_parses_base_state_without_optional_fields() {
    OssmStateInfo info;
    TEST_ASSERT_TRUE(parse(
        R"({"timestamp":1,"state":"menu.idle","speed":0,"stroke":50,"sensation":50,"depth":10,"buffer":100,"pattern":0,"position":0.00,"sessionId":"x"})",
        info));
    TEST_ASSERT_EQUAL_STRING("menu.idle", info.state.c_str());
    TEST_ASSERT_TRUE(info.error.empty());
    TEST_ASSERT_TRUE(info.pairingCode.empty());
    TEST_ASSERT_FALSE(info.isPaired);
}

void test_parses_update_failure() {
    OssmStateInfo info;
    TEST_ASSERT_TRUE(parse(R"({"state":"update.failed","error":"low-memory"})", info));
    TEST_ASSERT_EQUAL_STRING("update.failed", info.state.c_str());
    TEST_ASSERT_EQUAL_STRING("low-memory", info.error.c_str());
}

void test_parses_pairing_fields() {
    OssmStateInfo info;
    TEST_ASSERT_TRUE(parse(R"({"state":"pairing.idle","pairingCode":"AB12CD"})", info));
    TEST_ASSERT_EQUAL_STRING("AB12CD", info.pairingCode.c_str());
    TEST_ASSERT_FALSE(info.isPaired);
    TEST_ASSERT_TRUE(parse(R"({"state":"pairing.success.idle","isPaired":true})", info));
    TEST_ASSERT_TRUE(info.isPaired);
}

void test_parses_target_version() {
    OssmStateInfo info;
    TEST_ASSERT_TRUE(parse(R"({"state":"update.available","targetVersion":"1.0.58"})", info));
    TEST_ASSERT_EQUAL_STRING("1.0.58", info.targetVersion.c_str());
    TEST_ASSERT_TRUE(parse(R"({"state":"update.idle"})", info));
    TEST_ASSERT_TRUE(info.targetVersion.empty());
}

void test_rejects_garbage_and_missing_state() {
    OssmStateInfo info;
    TEST_ASSERT_FALSE(parse("not json", info));
    TEST_ASSERT_FALSE(parse(R"({"speed":10})", info));
    TEST_ASSERT_FALSE(parse("ok:go:update", info));
}

void test_state_prefix_helper() {
    TEST_ASSERT_TRUE(ossmStateStartsWith("update.installing", "update"));
    TEST_ASSERT_TRUE(ossmStateStartsWith("pairing.success.idle", "pairing"));
    TEST_ASSERT_FALSE(ossmStateStartsWith("menu.idle", "update"));
}

void test_error_reasons_are_user_text() {
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("wifi")));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("low-memory")));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("check-failed")));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("install-failed")));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("pairing-failed")));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(ossmErrorReason("something-new")));
}

void test_pairing_info_fields() {
    const std::string info = "AA:BB:CC:DD:EE:FF;ESP32;1;0123456789abcdef0123456789abcdef;1.2.3";
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", pairingInfoField(info, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("1.2.3", pairingInfoField(info, 4).c_str());
    TEST_ASSERT_EQUAL_STRING("", pairingInfoField(info, 9).c_str());
    TEST_ASSERT_TRUE(pairingInfoHasWifi(info));
    TEST_ASSERT_FALSE(pairingInfoHasWifi("AA;ESP32;0;md5;1.0.0"));
    TEST_ASSERT_FALSE(pairingInfoHasWifi(""));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parses_base_state_without_optional_fields);
    RUN_TEST(test_parses_update_failure);
    RUN_TEST(test_parses_pairing_fields);
    RUN_TEST(test_parses_target_version);
    RUN_TEST(test_rejects_garbage_and_missing_state);
    RUN_TEST(test_state_prefix_helper);
    RUN_TEST(test_error_reasons_are_user_text);
    RUN_TEST(test_pairing_info_fields);
    return UNITY_END();
}
