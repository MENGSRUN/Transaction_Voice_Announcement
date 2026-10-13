#include <Arduino.h>
#include <unity.h>
#include "audio_url_validation.h"

static const char* host = "voice-service-api.weknowyouknow.dev";

void test_accepts_exact_https_mp3_url() {
  TEST_ASSERT_TRUE(isAllowedMp3HttpsUrl(
      "https://voice-service-api.weknowyouknow.dev/media/voice.mp3", host));
}

void test_accepts_query_after_mp3_path() {
  TEST_ASSERT_TRUE(isAllowedMp3HttpsUrl(
      "https://voice-service-api.weknowyouknow.dev/media/voice.mp3?token=abc", host));
}

void test_rejects_insecure_scheme() {
  TEST_ASSERT_FALSE(isAllowedMp3HttpsUrl(
      "http://voice-service-api.weknowyouknow.dev/media/voice.mp3", host));
}

void test_rejects_host_suffix_attack() {
  TEST_ASSERT_FALSE(isAllowedMp3HttpsUrl(
      "https://voice-service-api.weknowyouknow.dev.attacker.test/media/voice.mp3", host));
}

void test_rejects_userinfo_attack() {
  TEST_ASSERT_FALSE(isAllowedMp3HttpsUrl(
      "https://voice-service-api.weknowyouknow.dev@attacker.test/media/voice.mp3", host));
}

void test_rejects_mp3_only_in_query() {
  TEST_ASSERT_FALSE(isAllowedMp3HttpsUrl(
      "https://voice-service-api.weknowyouknow.dev/media/voice?name=.mp3", host));
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_accepts_exact_https_mp3_url);
  RUN_TEST(test_accepts_query_after_mp3_path);
  RUN_TEST(test_rejects_insecure_scheme);
  RUN_TEST(test_rejects_host_suffix_attack);
  RUN_TEST(test_rejects_userinfo_attack);
  RUN_TEST(test_rejects_mp3_only_in_query);
  UNITY_END();
}

void loop() {}
