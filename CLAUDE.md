# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP8266 MiLight Hub is firmware for ESP8266/ESP32 microcontrollers that replaces Milight/LimitlessLED WiFi gateways. It enables control of Milight RGB/RGBW bulbs via WiFi with a web UI, MQTT integration, and REST API. Fork maintained at `TohaUA/esp8266_milight_hub`.

## Build Commands

### Firmware (PlatformIO)

```bash
pio run -e d1_mini          # Build for D1 Mini (most common target)
pio run -e nodemcuv2         # Build for NodeMCU v2/v3
pio run -e esp32             # Build for ESP32
pio run                      # Build all default environments
pio run -e nodemcuv2 --target upload --upload-port /dev/cu.usbserial-1120  # Upload via serial
```

Target environments: `nodemcuv2`, `d1_mini`, `esp12`, `esp07`, `huzzah`, `d1_mini_pro`, `esp32`

### Testing

```bash
# On-device tests (requires hardware connected)
pio test -e nodemcuv2 --upload-port /dev/cu.usbserial-1120 --test-port /dev/cu.usbserial-1120
pio test -e esp32 --upload-port /dev/cu.SLAB_USBtoUART --test-port /dev/cu.SLAB_USBtoUART
```

Tests are in `test/test_embedded/test_embedded.cpp` — shared across all environments. They cover packet formatters, group state, cache, persistence, and store operations. All 8 tests must pass on both ESP8266 and ESP32.

### Remote Debugging

Connect via `telnet <device-ip> 23` for live log streaming and interactive commands (`heap`, `status`, `uptime`, `sync`, `help`). All `DebugSerial.print*` calls mirror to both Serial and telnet.

### Web UI (web/)

```bash
cd web
npm install
npm run build               # Production build (embeds into firmware)
npm run build-dev           # Development build with source maps
npm run watch               # Watch mode (rebuilds on file changes)
```

Debug build flags (uncomment in `platformio.ini` `[base]` section):
- `-D DEBUG_PRINTF` - General debug output
- `-D MQTT_DEBUG` - MQTT protocol debugging
- `-D MILIGHT_UDP_DEBUG` - UDP gateway debugging
- `-D STATE_DEBUG` - State management debugging

## Architecture

### Build Pipeline

1. `.patch_rf24.py` — Pre-build script that patches RF24 1.5.0's `printf_P` macro conflict
2. `.build_web.py` — Pre-build script that runs `npm install && npm run build` for the web UI
3. `.get_version.py` — Derives version from `git describe --always`, passes as `-DMILIGHT_HUB_VERSION`
4. `web/build.mjs` — esbuild bundles React/TS into `dist/build/bundle.{js,css}`
5. `web/inline.js` — gzip-compresses bundles into C++ header files (`dist/*.gz.h`)

### platformio.ini Structure

ESP8266-only flags (`board_f_cpu`, `PIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48`, `HTTP_UPLOAD_BUFLEN=128`) are in `[esp8266]`, not `[base]`. ESP32 has its own flags (`HTTP_UPLOAD_BUFLEN=1024`, `-Wno-deprecated-declarations`). All `[env:*]` sections reference `${esp8266.build_flags}` or `${esp32.build_flags}`, not `${base.build_flags}`.

### Core Module Dependency Graph

```
main.cpp
  +-- WebServer/ --> MiLight/, MQTT/, MiLightState/, Settings/, Transitions/
  +-- MQTT/ --> MiLight/, MiLightState/, Settings/
  +-- MiLight/ --> Radio/, MiLightState/, Settings/, Transitions/, Types/
  +-- Radio/ --> Settings/, Types/
  +-- MiLightState/ --> Types/, DataStructures/, Environment/
  +-- Transitions/ --> DataStructures/, Types/
  +-- Types/, Helpers/, LEDStatus/, SSDP/  (leaf modules)
```

### Key Data Flows

**Command (HTTP/MQTT) → Radio TX:**
`MiLightHttpServer` or `MqttClient` → `MiLightClient::update()` → `PacketFormatter` (polymorphic) → `PacketSender::enqueue()` → `PacketQueue` (CircularBuffer) → `RadioSwitchboard::write()` → NRF24/LT8900 SPI

**Radio RX → State Update → MQTT + WebSocket:**
`handleListen()` → `PacketFormatter::parsePacket()` → `GroupStateStore::patch()` → `MqttClient::sendUpdate()` + `BulbStateUpdater::enqueueUpdate()` + `WebSocketsServer::broadcastTXT()`

### State Management

`GroupStateStore` is a facade over `GroupStateCache` (LRU, max 100 entries, `LinkedList`) and `GroupStatePersistence` (file-per-group on LittleFS, atomic write-then-rename). Dirty state is flushed one entry per `limitedFlush()` call (rate-limited, default 10s). On cache eviction, dirty state is persisted before removal.

### Debug Output

All `Serial.print*` calls have been replaced with `DebugSerial.print*` across 27 source files. `DebugSerial` (in `lib/Helpers/DebugSerial.h`) mirrors output to both hardware Serial and any connected ESPTelnet client (port 23). Output is buffered and flushed on newline. Zero overhead when no telnet client is connected.

### Adding a New Remote Type

1. Create a `PacketFormatter` subclass in `lib/MiLight/` (extend `V2PacketFormatter` for newer protocols)
2. Register it in `MiLightRemoteConfig.cpp`
3. Add the remote type enum in `lib/Types/MiLightRemoteType.h`

## Key Constraints

- **Memory**: ESP8266 has 80KB RAM, ~4KB stack. Avoid VLAs with user-controlled sizes, prefer fixed buffers. Use `const char*` over `String` in hot paths to reduce heap fragmentation.
- **ArduinoJson v7**: Uses elastic `JsonDocument` (no manual sizing). The `RichHttpServer` library still uses deprecated v6 API — suppressed with `-Wno-deprecated-declarations` on ESP32.
- **Radio**: One-way protocol — no acknowledgment from bulbs. Hub maintains authoritative state.
- **Filesystem**: LittleFS on both platforms. ESP32 requires `mkdir()` before creating files in subdirectories. All file writes use write-then-rename for atomicity.
- **MQTT**: Socket timeout is 2 seconds (not default 15). `BulbStateUpdater` skips flush when MQTT is disconnected. State is synced on MQTT reconnect via `syncAll()`.
- **WiFi**: Auto-reconnects on disconnect (`WiFi.setAutoReconnect(true)` + fallback every 30s).
- **Heap monitoring**: Logs warning at <8KB free, forces restart at <4KB (in `loop()`).
- **Web UI**: Entire React app gzipped into C++ byte arrays. Update checks point to `TohaUA/esp8266_milight_hub`.
