# Architecture Documentation

## System Context

```
                    +------------------+
                    |   Milight Bulbs  |
                    |  (2.4GHz radio)  |
                    +--------+---------+
                             |
                    RF (NRF24L01+/LT8900)
                             |
+-------------+    +---------+---------+    +----------------+
|   Web UI    |<-->|                   |<-->|  MQTT Broker   |
|  (Browser)  | 80 |   ESP8266/ESP32   | tcp| (Home Asst.)   |
+-------------+ 81 |   MiLight Hub    |    +----------------+
                    |                   |
+-------------+    |   Firmware v2.0   |    +----------------+
| Milight App |<-->|                   |<-->|  Telnet Client |
| (UDP v5/v6) | udp+-------------------+ 23 | (Debug)        |
+-------------+                             +----------------+
```

The hub replaces a Milight WiFi gateway. It sits between WiFi clients (browser, MQTT, UDP) and Milight bulbs (2.4GHz radio). It maintains authoritative state server-side since the radio protocol is one-way (no feedback from bulbs).

## External Interfaces

| Interface | Port | Protocol | Purpose |
|-----------|------|----------|---------|
| HTTP | 80 | REST + Web UI | Device control, settings, firmware OTA |
| WebSocket | 81 | WS | Real-time packet/state broadcast to browser |
| MQTT | config | TCP | Bidirectional control + state sync with Home Assistant |
| UDP v5/v6 | config | UDP | Legacy Milight gateway emulation |
| SSDP | 80 | UDP multicast | UPnP device discovery |
| mDNS | 5353 | UDP multicast | `milight-hub.local` hostname |
| Telnet | 23 | TCP | Debug console (`heap`, `status`, `uptime`, `sync`) |

## Module Architecture

```
main.cpp (entry point, wires everything together)
  |
  +-- WebServer/          HTTP REST API, WebSocket, OTA firmware
  |     +-- MiLightHttpServer
  |
  +-- MQTT/               MQTT client, state publishing, HA discovery
  |     +-- MqttClient
  |     +-- BulbStateUpdater
  |     +-- HomeAssistantDiscoveryClient
  |
  +-- MiLight/            Protocol engine (core business logic)
  |     +-- MiLightClient         Command dispatcher
  |     +-- PacketFormatter       Base class (7 protocol subclasses)
  |     +-- RadioSwitchboard      Radio multiplexer
  |     +-- PacketSender          TX queue with throttling
  |     +-- PacketQueue           Circular buffer
  |     +-- MiLightRemoteConfig   Remote type registry
  |     +-- V2RFEncoding          V2 packet encryption/decryption
  |
  +-- Radio/              Hardware abstraction
  |     +-- MiLightRadio (abstract)
  |     +-- NRF24MiLightRadio     NRF24L01+ via PL1167_nRF24
  |     +-- LT8900MiLightRadio    LT8900 direct
  |     +-- MiLightRadioFactory   Factory for radio selection
  |
  +-- MiLightState/       State management
  |     +-- GroupState            Per-group bulb state (8 bytes)
  |     +-- GroupStateCache       LRU cache (LinkedList)
  |     +-- GroupStatePersistence File-per-group on LittleFS
  |     +-- GroupStateStore       Facade: cache + persistence
  |
  +-- Udp/                Legacy gateway emulation
  |     +-- V5MiLightUdpServer
  |     +-- V6MiLightUdpServer + command handlers
  |     +-- MiLightDiscoveryServer
  |
  +-- Transitions/        Smooth state interpolation
  |     +-- TransitionController
  |     +-- FieldTransition / ColorTransition
  |
  +-- Settings/           Configuration persistence
  |     +-- Settings (JSON on LittleFS)
  |     +-- BackupManager
  |     +-- AboutHelper
  |
  +-- Types/              Foundational value types
  |     +-- BulbId, GroupStateField, MiLightRemoteType, ParsedColor
  |
  +-- Helpers/            Utilities
        +-- DebugSerial (Serial + Telnet mirror)
        +-- ColorUtils, IntParsing, Units
```

## Data Flows

### HTTP/MQTT Command --> Radio TX

```
HTTP PUT /gateways/:id/:type/:group  -or-  MQTT message on command topic
  |
  v
MiLightHttpServer::handleUpdateGroup()  -or-  MqttClient::publishCallback()
  |  parse JSON body, resolve device/group/type
  v
MiLightClient::prepare(config, deviceId, groupId)
  |  select PacketFormatter, configure radio
  v
MiLightClient::update(jsonObject)
  |  iterate FIELD_SETTERS[] in FIELD_ORDERINGS[] order
  |  each setter calls PacketFormatter method (e.g., updateBrightness)
  v
PacketFormatter::flushPacket()  -->  PacketSender::enqueue()
  |                                       |
  v                                       v  (async, from main loop)
PacketQueue (CircularBuffer)        PacketSender::loop()
                                          |
                                          v
                                    RadioSwitchboard::write()
                                          |
                                          v
                                    NRF24MiLightRadio::write()  (SPI to hardware)
```

### Radio RX --> State Update --> MQTT + WebSocket

```
main loop: handleListen()
  |  cycle through radio configs (round-robin)
  v
RadioSwitchboard::available() + read()
  |  raw 9-byte packet from NRF24
  v
MiLightRemoteConfig::fromReceivedPacket()
  |  identify remote type
  v
onPacketSentHandler()
  |
  +---> PacketFormatter::parsePacket()     decode into JSON
  +---> GroupStateStore::get() + patch()   update cached state
  +---> stateStore->set()                  mark dirty, fan out group 0
  +---> MqttClient::sendUpdate()           publish delta (QoS 0)
  +---> BulbStateUpdater::enqueueUpdate()  queue full state (rate-limited)
  +---> MiLightHttpServer::handlePacketSent()
            |
            v
        WebSocketsServer::broadcastTXT()   push to all WS clients
```

### State Persistence

```
GroupStateStore (facade)
  |
  +-- GroupStateCache (LRU, max 100 entries)
  |     |  get(): cache hit --> return pointer
  |     |  get(): cache miss --> load from persistence, insert into cache
  |     |  set(): update in-place, mark dirty
  |     |  eviction: flush dirty state to persistence before evicting
  |     |
  |     +-- LinkedList<GroupCacheNode*>
  |           head = MRU, tail = LRU
  |
  +-- GroupStatePersistence (file-per-group)
  |     |  path: /group_states/<compactId_hex>
  |     |  format: 8 bytes raw binary (GroupState::StateData union)
  |     |  atomic write: .tmp file + rename
  |     |
  |     +-- LittleFS
  |
  +-- limitedFlush() (called each loop, rate-limited by stateFlushInterval)
        |  flushes ONE dirty entry per call
        |  default interval: 10 seconds
```

## Design Patterns

| Pattern | Where | Purpose |
|---------|-------|---------|
| **Strategy** | `PacketFormatter` hierarchy (7 subclasses) | Different Milight protocols |
| **Abstract Factory** | `MiLightRadioFactory` | NRF24 vs LT8900 radio selection |
| **Observer** | `PacketSentHandler`, `EventHandler`, `onSettingsSaved` | Decouple components |
| **Facade** | `GroupStateStore` | Unify cache + persistence |
| **Command Table** | `FIELD_SETTERS[]` + `FIELD_ORDERINGS[]` | JSON field → handler dispatch |
| **LRU Cache** | `GroupStateCache` | Bounded in-memory state |
| **Producer-Consumer** | `PacketSender` + `PacketQueue` | Decouple command processing from radio TX |
| **Factory Method** | `MiLightUdpServer::fromVersion()` | UDP v5/v6 server creation |
| **Adapter** | `PL1167_nRF24` | NRF24L01+ → MiLight radio protocol |
| **Pacing** | `DiscoveryPacer` | 1 HA config per loop iteration |

## Threading Model

**Single-threaded cooperative** on both ESP8266 and ESP32. One `loop()` function services all subsystems sequentially:

```
loop() {
  ledStatus->handle()
  shouldRestart() check
  wifiManager->process()

  if (WiFi connected) {
    httpServer->handleClient()        // HTTP + WebSocket
    mqttClient->handleClient()        // MQTT
    bulbStateUpdater->loop()          // Rate-limited MQTT state publish
    discoveryPacer.loop()             // HA discovery (1 per tick)
    udpServers->handleClient()        // UDP gateways
    discoveryServer->handleClient()   // SSDP discovery
    handleListen()                    // Radio RX
    MDNS.update()                     // ESP8266 only
    stateStore->limitedFlush()        // Persist dirty state
    packetSender->loop()              // Radio TX
    transitions.loop()                // Smooth transitions
    telnet.loop()                     // Debug console
  }

  WiFi reconnection (every 30s if disconnected)
  Heap monitoring (every 60s, restart if <4KB)
}
```

**Rate limiting** prevents any subsystem from starving others:
- `PacketSender`: adaptive repeat count based on inter-command timing
- `BulbStateUpdater`: rate-limited MQTT state (default 500ms)
- `GroupStateStore`: rate-limited filesystem writes (default 10s)
- `DiscoveryPacer`: 1 HA discovery message per loop iteration
- `handleListen()`: skipped while `packetSender->isSending()`

## Key Constraints

- **ESP8266**: 80KB RAM, 4KB stack, 160MHz single core
- **ESP32**: 520KB RAM, 8KB stack, 240MHz dual core (but firmware is single-threaded)
- **Radio**: One-way protocol — no acknowledgment from bulbs
- **State**: Server-side authoritative — hub tracks what it believes bulb state to be
- **Web UI**: Entire React app gzipped into C++ byte arrays embedded in firmware
- **Filesystem**: LittleFS with atomic writes (write-then-rename)

## Architecture Decisions

### ADR-001: LittleFS over SPIFFS
- **Decision**: Migrated from SPIFFS to LittleFS
- **Rationale**: SPIFFS is deprecated, LittleFS supports `rename()` (enables atomic writes), better wear leveling
- **Trade-off**: First boot after migration reformats filesystem (settings lost)

### ADR-002: ArduinoJson v7
- **Decision**: Migrated from ArduinoJson v6 to v7
- **Rationale**: v7 uses elastic `JsonDocument` (no more manual sizing), cleaner API
- **Trade-off**: v7 allocates on heap (was stack for `StaticJsonDocument`); RichHttpServer library still uses v6 API (suppressed with `-Wno-deprecated-declarations` on ESP32)

### ADR-003: Cooperative single-threading
- **Decision**: No FreeRTOS tasks, even on ESP32
- **Rationale**: Simplicity, no mutex/synchronization needed, matches ESP8266 model
- **Trade-off**: Long operations (MQTT connect, radio TX bursts) must yield() to avoid WDT

### ADR-004: Static FIELD_SETTERS array over std::map
- **Decision**: Replaced `std::map<const char*, std::function<...>>` with static array of function pointers
- **Rationale**: Saves ~730 bytes heap, eliminates 14 heap allocations, faster linear scan for 13 entries
- **Trade-off**: O(n) lookup instead of O(log n), but n=13 makes linear scan faster in practice

### ADR-005: DebugSerial wrapper for telnet mirroring
- **Decision**: All `Serial.print` calls redirected through `DebugSerial` which writes to both Serial and telnet
- **Rationale**: Remote debugging without physical serial connection
- **Trade-off**: 256-byte buffer overhead, negligible CPU cost when telnet disconnected

### ADR-006: Atomic writes for state persistence
- **Decision**: Write to `.tmp` file then `rename()` for both Settings and GroupStatePersistence
- **Rationale**: Prevents data corruption on power loss (old file intact until rename succeeds)
- **Trade-off**: Requires LittleFS (SPIFFS doesn't support rename)
