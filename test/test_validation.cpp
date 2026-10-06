#include "Arduino.h"
#include <unity.h>
#include "../include/validation.h"
#include "../src/validation.cpp"

void setUp() {}
void tearDown() {}

void test_isValidUid_valid() {
    TEST_ASSERT_TRUE(isValidUid("01A2B3C4"));
    TEST_ASSERT_TRUE(isValidUid("ABCDEF123456"));
    TEST_ASSERT_TRUE(isValidUid("0123456789ABCDEF"));
}

void test_isValidUid_invalid() {
    TEST_ASSERT_FALSE(isValidUid(""));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4G"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4H"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4I"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4J"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4K"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4L"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4M"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4N"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4O"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4P"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4Q"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4R"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4S"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4T"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4U"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4V"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4W"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4X"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4Y"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4Z"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4!"));
    TEST_ASSERT_FALSE(isValidUid("01A2B3C4 "));
    TEST_ASSERT_TRUE(isValidUid("01a2b3c4")); // lowercase should be valid
}

void test_isValidHolderName_valid() {
    TEST_ASSERT_TRUE(isValidHolderName("User"));
    TEST_ASSERT_TRUE(isValidHolderName("John Doe"));
    TEST_ASSERT_TRUE(isValidHolderName("User 1"));
    TEST_ASSERT_TRUE(isValidHolderName(String(32, 'A')));
}

void test_isValidHolderName_invalid() {
    TEST_ASSERT_FALSE(isValidHolderName(""));
    TEST_ASSERT_FALSE(isValidHolderName(String(33, 'A')));
}

void test_isValidSsid_valid() {
    TEST_ASSERT_TRUE(isValidSsid("MyNetwork"));
    TEST_ASSERT_TRUE(isValidSsid("My Network 123"));
    TEST_ASSERT_TRUE(isValidSsid(String(32, 'A')));
}

void test_isValidSsid_invalid() {
    TEST_ASSERT_FALSE(isValidSsid(""));
    TEST_ASSERT_FALSE(isValidSsid(String(33, 'A')));
}

void test_isValidPassword_valid() {
    TEST_ASSERT_TRUE(isValidPassword("password123"));
    TEST_ASSERT_TRUE(isValidPassword("MyPass1234"));
    TEST_ASSERT_TRUE(isValidPassword(String(63, 'A')));
}

void test_isValidPassword_invalid() {
    TEST_ASSERT_FALSE(isValidPassword(""));
    TEST_ASSERT_FALSE(isValidPassword("short"));
    TEST_ASSERT_FALSE(isValidPassword(String(64, 'A')));
}

void test_isValidUrl_valid() {
    TEST_ASSERT_TRUE(isValidUrl("http://example.com"));
    TEST_ASSERT_TRUE(isValidUrl("https://example.com"));
    TEST_ASSERT_TRUE(isValidUrl("https://script.google.com/macros/s/AKfycbw.../exec"));
}

void test_isValidUrl_invalid() {
    TEST_ASSERT_FALSE(isValidUrl(""));
    TEST_ASSERT_FALSE(isValidUrl("ftp://example.com"));
    TEST_ASSERT_FALSE(isValidUrl("example.com"));
    TEST_ASSERT_FALSE(isValidUrl("http://"));
    TEST_ASSERT_FALSE(isValidUrl("not-a-url"));
    TEST_ASSERT_FALSE(isValidUrl(String(257, 'a')));
}

void test_isValidUnlockMs_valid() {
    TEST_ASSERT_TRUE(isValidUnlockMs(500));
    TEST_ASSERT_TRUE(isValidUnlockMs(1000));
    TEST_ASSERT_TRUE(isValidUnlockMs(3000));
    TEST_ASSERT_TRUE(isValidUnlockMs(30000));
}

void test_isValidUnlockMs_invalid() {
    TEST_ASSERT_FALSE(isValidUnlockMs(0));
    TEST_ASSERT_FALSE(isValidUnlockMs(499));
    TEST_ASSERT_FALSE(isValidUnlockMs(30001));
    TEST_ASSERT_FALSE(isValidUnlockMs(65535));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_isValidUid_valid);
    RUN_TEST(test_isValidUid_invalid);
    RUN_TEST(test_isValidHolderName_valid);
    RUN_TEST(test_isValidHolderName_invalid);
    RUN_TEST(test_isValidSsid_valid);
    RUN_TEST(test_isValidSsid_invalid);
    RUN_TEST(test_isValidPassword_valid);
    RUN_TEST(test_isValidPassword_invalid);
    RUN_TEST(test_isValidUrl_valid);
    RUN_TEST(test_isValidUrl_invalid);
    RUN_TEST(test_isValidUnlockMs_valid);
    RUN_TEST(test_isValidUnlockMs_invalid);
    UNITY_END();
}