#include <Arduino.h>
#include <unity.h>
#include "audio_stream_buffer.h"

class FragmentedSource : public AudioFileSource {
public:
  uint32_t read(void* data, uint32_t len) override {
    uint32_t count = len < 32 - position ? len : 32 - position;
    auto* target = static_cast<uint8_t*>(data);
    for (uint32_t i = 0; i < count; i++) target[i] = position++;
    return count;
  }
  uint32_t readNonBlock(void* data, uint32_t len) override {
    // Simulate temporary network silence and fragmented TLS reads.
    if (++polls % 3 == 0) return 0;
    return read(data, len < 3 ? len : 3);
  }
  bool isOpen() override { return true; }
  bool close() override { return true; }
  uint32_t getSize() override { return 32; }
  uint32_t getPos() override { return position; }
private:
  uint32_t position = 0;
  uint32_t polls = 0;
};

void test_preserves_bytes_across_wrap_underflow_and_refill() {
  FragmentedSource source;
  uint8_t memory[8];
  AudioStreamBuffer buffer(&source, memory, sizeof(memory));
  TEST_ASSERT_TRUE(buffer.prime());
  uint8_t actual[32] = {};
  uint32_t consumed = 0;
  while (consumed < sizeof(actual)) {
    buffer.loop();
    // Reads larger than the FIFO trigger the old library's lossy refill path.
    uint32_t count = buffer.read(actual + consumed,
                                consumed == 0 ? 13 : sizeof(actual) - consumed);
    TEST_ASSERT_GREATER_THAN(0, count);
    consumed += count;
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(memory), buffer.getFillLevel());
  }
  for (uint32_t i = 0; i < sizeof(actual); i++) TEST_ASSERT_EQUAL_UINT8(i, actual[i]);
  TEST_ASSERT_EQUAL_UINT32(32, buffer.getPos());
  TEST_ASSERT_EQUAL_UINT32(0, buffer.read(actual, 1));
}

void test_background_refill_before_first_read_is_safe() {
  FragmentedSource source;
  uint8_t memory[8], actual[8];
  AudioStreamBuffer buffer(&source, memory, sizeof(memory));
  buffer.loop();
  buffer.loop();
  TEST_ASSERT_EQUAL_UINT32(8, buffer.read(actual, sizeof(actual)));
  for (uint32_t i = 0; i < sizeof(actual); i++) TEST_ASSERT_EQUAL_UINT8(i, actual[i]);
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_preserves_bytes_across_wrap_underflow_and_refill);
  RUN_TEST(test_background_refill_before_first_read_is_safe);
  UNITY_END();
}
void loop() {}
