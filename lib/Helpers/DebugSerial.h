#pragma once

#include <Arduino.h>
#include <ESPTelnet.h>

// A Print wrapper that mirrors output to both Serial and ESPTelnet.
// All existing Serial.print/println/printf calls get redirected here
// via the #define at the bottom of this file.
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
      telnet->write(c);
    }
    return 1;
  }

  virtual size_t write(const uint8_t* buffer, size_t size) override {
    Serial.write(buffer, size);
    if (telnet && telnet->isConnected()) {
      for (size_t i = 0; i < size; i++) {
        telnet->write(buffer[i]);
      }
    }
    return size;
  }

  // Support printf (ESP8266/ESP32 Print class has printf)
  using Print::printf;

private:
  ESPTelnet* telnet = nullptr;
};

extern DebugSerialClass DebugSerial;
