#pragma once

#include <Arduino.h>
#include <ESPTelnet.h>

// A Print wrapper that mirrors output to both Serial and ESPTelnet.
// Buffers telnet output and flushes on newline for clean line-based output.
class DebugSerialClass : public Print {
public:
  void begin(unsigned long baud) {
    Serial.begin(baud);
  }

  void setTelnet(ESPTelnet* t) {
    telnet = t;
  }

  virtual size_t write(uint8_t c) override {
    Serial.write(c);
    if (telnet && telnet->isConnected()) {
      if (bufPos < sizeof(buf) - 1) {
        buf[bufPos++] = c;
      }
      if (c == '\n' || bufPos >= sizeof(buf) - 1) {
        flushTelnet();
      }
    }
    return 1;
  }

  virtual size_t write(const uint8_t* buffer, size_t size) override {
    Serial.write(buffer, size);
    if (telnet && telnet->isConnected()) {
      for (size_t i = 0; i < size; i++) {
        if (bufPos < sizeof(buf) - 1) {
          buf[bufPos++] = buffer[i];
        }
        if (buffer[i] == '\n' || bufPos >= sizeof(buf) - 1) {
          flushTelnet();
        }
      }
    }
    return size;
  }

  using Print::printf;

private:
  ESPTelnet* telnet = nullptr;
  char buf[256];
  size_t bufPos = 0;

  void flushTelnet() {
    if (bufPos > 0 && telnet && telnet->isConnected()) {
      buf[bufPos] = '\0';
      telnet->print(buf);
      bufPos = 0;
    }
  }
};

extern DebugSerialClass DebugSerial;
