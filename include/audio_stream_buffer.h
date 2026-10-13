#pragma once

#include "AudioFileSource.h"

// A bounded FIFO. Refills never reset or discard bytes already received.
class AudioStreamBuffer : public AudioFileSource {
public:
  AudioStreamBuffer(AudioFileSource* source, uint8_t* memory, uint32_t capacity)
      : source(source), memory(memory), capacity(capacity) {}

  bool prime() {
    if (!source || !memory || !capacity) return false;
    used = source->read(memory, capacity);
    writePosition = used % capacity;
    return used > 0;
  }

  uint32_t read(void* data, uint32_t len) override {
    if (!data || !len || !memory) return 0;
    auto* target = static_cast<uint8_t*>(data);
    uint32_t total = 0;
    while (total < len) {
      if (!used) {
        // Only block for the bytes the decoder needs, not a full refill.
        uint32_t wanted = len - total;
        if (wanted > capacity) wanted = capacity;
        used = source->read(memory, wanted);
        readPosition = 0;
        writePosition = used % capacity;
        if (!used) break;
      }
      uint32_t count = len - total;
      if (count > used) count = used;
      if (count > capacity - readPosition) count = capacity - readPosition;
      memcpy(target + total, memory + readPosition, count);
      readPosition = (readPosition + count) % capacity;
      used -= count;
      total += count;
      consumed += count;
    }
    return total;
  }

  bool loop() override {
    if (!source || !memory || !capacity) return false;
    // At most one nonblocking read per loop keeps audio DMA serviced.
    uint32_t room = capacity - used;
    uint32_t contiguous = capacity - writePosition;
    if (room > contiguous) room = contiguous;
    if (room) {
      uint32_t count = source->readNonBlock(memory + writePosition, room);
      used += count;
      writePosition = (writePosition + count) % capacity;
    }
    return true;
  }

  bool close() override { return source->close(); }
  bool isOpen() override { return source && source->isOpen(); }
  uint32_t getSize() override { return source->getSize(); }
  uint32_t getPos() override { return consumed; }
  uint32_t getFillLevel() const { return used; }

private:
  AudioFileSource* source;
  uint8_t* memory;
  uint32_t capacity;
  uint32_t readPosition = 0;
  uint32_t writePosition = 0;
  uint32_t used = 0;
  uint32_t consumed = 0;
};
