#pragma once

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "AudioFileSource.h"

// Own the TLS connection for the entire decoder lifetime. Never reconnect
// midway through playback: restarting a response would replay already heard audio.
class AudioHttpsStream : public AudioFileSource {
public:
  AudioHttpsStream(const char* ca, uint32_t connectMs, uint32_t requestMs,
                   uint32_t readMs, uint32_t maxBytes)
      : readTimeoutMs(readMs), maximumBytes(maxBytes) {
    secureClient.setCACert(ca);
    secureClient.setHandshakeTimeout((connectMs + 999) / 1000);
    http.setConnectTimeout(connectMs);
    http.setTimeout(requestMs);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    // Raw stream reads require an unencoded, non-chunked response.
    http.useHTTP10(true);
  }

  ~AudioHttpsStream() override { close(); }

  bool open(const char* url) override {
    close();
    position = 0;
    size = 0;
    failed = false;
    retryable = true;
    if (!http.begin(secureClient, url)) return false;
    http.addHeader("Accept-Encoding", "identity");
    const char* headers[] = {"Transfer-Encoding", "Content-Encoding"};
    http.collectHeaders(headers, 2);
    unsigned long startedAt = millis();
    int code = http.GET();
    Serial.printf("HTTPS stream connect/headers: %lu ms, code: %d\n",
                  millis() - startedAt, code);
    if (code != HTTP_CODE_OK) {
      retryable = code < 0 || code == 408 || code == 429 || code >= 500;
      close();
      return false;
    }
    int contentLength = http.getSize();
    String encoding = http.header("Content-Encoding");
    if (contentLength <= 0 || (uint32_t)contentLength > maximumBytes ||
        !http.header("Transfer-Encoding").isEmpty() ||
        (!encoding.isEmpty() && !encoding.equalsIgnoreCase("identity"))) {
      Serial.println("Rejected stream: requires bounded Content-Length and an unencoded body");
      retryable = false;
      close();
      return false;
    }
    size = contentLength;
    opened = true;
    Serial.printf("HTTPS MP3 stream opened: %u bytes\n", (unsigned)size);
    return true;
  }

  uint32_t read(void* data, uint32_t len) override {
    if (!opened || failed || !data || position >= size) return 0;
    uint32_t wanted = len < size - position ? len : size - position;
    uint32_t total = 0;
    unsigned long lastProgress = millis();
    while (total < wanted) {
      uint32_t count = readNonBlock(static_cast<uint8_t*>(data) + total, wanted - total);
      if (count) {
        total += count;
        lastProgress = millis();
      } else if (failed) {
        break;
      } else if (millis() - lastProgress >= readTimeoutMs) {
        failed = true;
        Serial.printf("HTTPS audio stream stalled: %u/%u bytes received\n",
                      (unsigned)position, (unsigned)size);
        break;
      } else {
        delay(1);
      }
    }
    return total;
  }

  uint32_t readNonBlock(void* data, uint32_t len) override {
    if (!opened || failed || !data || !len || position >= size) return 0;
    auto* stream = http.getStreamPtr();
    int available = stream->available();
    if (available <= 0) {
      if (!http.connected()) {
        failed = true;
        Serial.printf("HTTPS stream truncated: %u/%u bytes\n",
                      (unsigned)position, (unsigned)size);
      }
      return 0;
    }
    uint32_t count = len < size - position ? len : size - position;
    if (count > (uint32_t)available) count = available;
    int received = stream->read(static_cast<uint8_t*>(data), count);
    if (received <= 0) return 0;
    position += received;
    return received;
  }

  bool close() override {
    http.end();
    opened = false;
    return true;
  }
  bool isOpen() override { return opened; }
  uint32_t getSize() override { return size; }
  uint32_t getPos() override { return position; }
  bool transferComplete() const { return !failed && size > 0 && position == size; }
  bool canRetry() const { return retryable; }
  bool hasFailed() const { return failed; }

private:
  WiFiClientSecure secureClient;
  HTTPClient http;
  uint32_t readTimeoutMs;
  uint32_t maximumBytes;
  uint32_t size = 0;
  uint32_t position = 0;
  bool opened = false;
  bool failed = false;
  bool retryable = true;
};
