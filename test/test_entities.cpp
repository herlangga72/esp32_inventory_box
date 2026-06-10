#include <unity.h>
#include "../src/domain/entities/Tool.h"
#include "../src/domain/entities/User.h"
#include <cstring>

// ==================== Tool ====================

void test_tool_default_ctor() {
    Tool t;
    TEST_ASSERT_EQUAL(0, t.id);
    TEST_ASSERT_EQUAL_STRING("", t.name);
    TEST_ASSERT_EQUAL_FLOAT(0.0, t.weightGrams);
    TEST_ASSERT_EQUAL_FLOAT(Config::DEFAULT_TOLERANCE, t.toleranceGrams);
    TEST_ASSERT_TRUE(t.active);
}

void test_tool_set_name() {
    Tool t;
    t.setName("Hammer");
    TEST_ASSERT_EQUAL_STRING("Hammer", t.name);
}

void test_tool_set_name_truncation() {
    Tool t;
    t.setName("ThisIsAVeryLongToolNameThatExceedsThirtyTwoCharacters");
    TEST_ASSERT_EQUAL(strlen(t.name), 31);
}

void test_tool_matches_exact() {
    Tool t;
    t.weightGrams = 500.0;
    t.toleranceGrams = 5.0;
    TEST_ASSERT_TRUE(t.matchesWeight(500.0, 500.0));
}

void test_tool_matches_within_tolerance_above() {
    Tool t;
    t.weightGrams = 500.0;
    t.toleranceGrams = 5.0;
    TEST_ASSERT_TRUE(t.matchesWeight(503.0, 503.0));
}

void test_tool_matches_within_tolerance_below() {
    Tool t;
    t.weightGrams = 500.0;
    t.toleranceGrams = 5.0;
    TEST_ASSERT_TRUE(t.matchesWeight(498.0, 498.0));
}

void test_tool_matches_at_boundary() {
    Tool t;
    t.weightGrams = 500.0;
    t.toleranceGrams = 5.0;
    TEST_ASSERT_TRUE(t.matchesWeight(505.0, 505.0));
}

void test_tool_not_matches_outside() {
    Tool t;
    t.weightGrams = 500.0;
    t.toleranceGrams = 5.0;
    TEST_ASSERT_FALSE(t.matchesWeight(510.0, 510.0));
}

void test_tool_zero_tolerance() {
    Tool t;
    t.weightGrams = 100.0;
    t.toleranceGrams = 0.0;
    TEST_ASSERT_TRUE(t.matchesWeight(100.0, 100.0));
    TEST_ASSERT_FALSE(t.matchesWeight(101.0, 101.0));
}

void test_tool_size() {
    // ESP32 (32-bit): ≤56. x86_64: larger due to 8-byte time_t + alignment.
    // This test validates the ESP32 static_assert; informational on native.
#if UINTPTR_MAX == UINT32_MAX
    TEST_ASSERT_LESS_OR_EQUAL(56, sizeof(Tool));
#else
    TEST_ASSERT_LESS_OR_EQUAL(72, sizeof(Tool));  // 64-bit padding
#endif
}

// ==================== User ====================

void test_user_default_ctor() {
    User u;
    TEST_ASSERT_EQUAL(0, u.id);
    TEST_ASSERT_EQUAL_STRING("", u.name);
    TEST_ASSERT_EQUAL_STRING("", u.pin);
    TEST_ASSERT_TRUE(u.active);
    TEST_ASSERT_EQUAL(0, u.fpId);
    TEST_ASSERT_EQUAL(0, u.sessionCount);
}

void test_user_set_name() {
    User u;
    u.setName("Herlangga");
    TEST_ASSERT_EQUAL_STRING("Herlangga", u.name);
}

void test_user_set_pin() {
    User u;
    u.setPin("1234");
    TEST_ASSERT_EQUAL_STRING("1234", u.pin);
}

void test_user_set_pin_truncation() {
    User u;
    u.setPin("12345");
    TEST_ASSERT_EQUAL_STRING("1234", u.pin);
}

void test_user_validate_pin_correct() {
    User u;
    u.setPin("5678");
    TEST_ASSERT_TRUE(u.validatePin("5678"));
}

void test_user_validate_pin_wrong() {
    User u;
    u.setPin("5678");
    TEST_ASSERT_FALSE(u.validatePin("0000"));
}

void test_user_validate_pin_empty() {
    User u;
    u.setPin("");
    TEST_ASSERT_TRUE(u.validatePin(""));
    TEST_ASSERT_FALSE(u.validatePin("1234"));
}

void test_user_size() {
#if UINTPTR_MAX == UINT32_MAX
    TEST_ASSERT_LESS_OR_EQUAL(72, sizeof(User));
#else
    TEST_ASSERT_LESS_OR_EQUAL(96, sizeof(User));  // 64-bit padding
#endif
}
