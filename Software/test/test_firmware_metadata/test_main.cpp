#include <cstring>
#include <unity.h>

void test_firmware_metadata_is_embedded() {
    TEST_ASSERT_EQUAL_STRING("test", FIRMWARE_TRACK);
    TEST_ASSERT_GREATER_THAN(0, std::strlen(FIRMWARE_BUILD_SHA));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_firmware_metadata_is_embedded);
    return UNITY_END();
}
