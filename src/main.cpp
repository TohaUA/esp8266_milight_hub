#ifndef UNIT_TEST

#  include <WiFiManager.h>
#  include <ArduinoJson.h>
#  include <cstdlib>
#  include <FS.h>
#  include <IntParsing.h>
#  include <LinkedList.h>
#  include <LEDStatus.h>
#  include <GroupStateStore.h>
#  include <MiLightRadioConfig.h>
#  include <MiLightRemoteConfig.h>
#  include <MiLightHttpServer.h>
#  include <Settings.h>
#  include <MiLightUdpServer.h>
#  include <MqttClient.h>
#  include <MiLightDiscoveryServer.h>
#  include <MiLightClient.h>
#  include <BulbStateUpdater.h>
#  include <RadioSwitchboard.h>
#  include <PacketSender.h>
#  include <HomeAssistantDiscoveryClient.h>
#  include <TransitionController.h>
#  include <ProjectWifi.h>
#  include <ESPTelnet.h>
#  include <DebugSerial.h>
#  include <AboutHelper.h>

#  include <ESPId.h>

#  ifdef ESP8266
#    include <ESP8266mDNS.h>
#    include <ESP8266SSDP.h>
#  elif defined(ESP32)
#    include "ESP32SSDP.h"
#    include <esp_wifi.h>
#    include <ESPmDNS.h>
#  endif

#  include <vector>
#  include <memory>
#  include "ProjectFS.h"

WiFiManager* wifiManager;

static LEDStatus* ledStatus;

Settings settings;

// Track WiFi settings to detect changes requiring restart
String prevWifiSsid, prevWifiPassword, prevWifiSsidSecondary, prevWifiPasswordSecondary;
String prevWifiStaticIP, prevWifiStaticIPGateway, prevWifiStaticIPNetmask, prevWifiDns;
bool prevWifiPortalOnFail;

void snapshotWifiSettings() {
  prevWifiSsid = settings.wifiSsid;
  prevWifiPassword = settings.wifiPassword;
  prevWifiSsidSecondary = settings.wifiSsidSecondary;
  prevWifiPasswordSecondary = settings.wifiPasswordSecondary;
  prevWifiStaticIP = settings.wifiStaticIP;
  prevWifiStaticIPGateway = settings.wifiStaticIPGateway;
  prevWifiStaticIPNetmask = settings.wifiStaticIPNetmask;
  prevWifiDns = settings.wifiDns;
  prevWifiPortalOnFail = settings.wifiPortalOnFail;
}

bool wifiSettingsChanged() {
  return prevWifiSsid != settings.wifiSsid || prevWifiPassword != settings.wifiPassword ||
         prevWifiSsidSecondary != settings.wifiSsidSecondary ||
         prevWifiPasswordSecondary != settings.wifiPasswordSecondary || prevWifiStaticIP != settings.wifiStaticIP ||
         prevWifiStaticIPGateway != settings.wifiStaticIPGateway ||
         prevWifiStaticIPNetmask != settings.wifiStaticIPNetmask || prevWifiDns != settings.wifiDns ||
         prevWifiPortalOnFail != settings.wifiPortalOnFail;
}

MiLightClient* milightClient = NULL;
RadioSwitchboard* radios = nullptr;
PacketSender* packetSender = nullptr;
std::shared_ptr<MiLightRadioFactory> radioFactory;
MiLightHttpServer* httpServer = NULL;
MqttClient* mqttClient = NULL;
MiLightDiscoveryServer* discoveryServer = NULL;
uint8_t currentRadioType = 0;

// For tracking and managing group state
GroupStateStore* stateStore = NULL;
BulbStateUpdater* bulbStateUpdater = NULL;
TransitionController transitions;

ESPTelnet telnet;

std::vector<std::shared_ptr<MiLightUdpServer>> udpServers;

// Paces HA discovery messages: sends 1 per loop iteration instead of all at once
struct DiscoveryPacer {
  enum State { IDLE, SENDING_CONFIGS, REMOVING_OLD };

  State state = IDLE;
  std::map<String, GroupAlias>::const_iterator addIt;
  std::map<String, GroupAlias>::const_iterator addEnd;
  std::map<uint32_t, BulbId>::const_iterator removeIt;
  std::map<uint32_t, BulbId>::const_iterator removeEnd;

  void begin() {
    if (settings.homeAssistantDiscoveryPrefix.length() == 0 || mqttClient == NULL) {
      return;
    }
    addIt = settings.groupIdAliases.begin();
    addEnd = settings.groupIdAliases.end();
    removeIt = settings.deletedGroupIdAliases.begin();
    removeEnd = settings.deletedGroupIdAliases.end();
    state = SENDING_CONFIGS;
  }

  void loop() {
    if (state == IDLE || mqttClient == NULL)
      return;

    if (state == SENDING_CONFIGS) {
      if (addIt != addEnd) {
        HomeAssistantDiscoveryClient discoveryClient(settings, mqttClient);
        discoveryClient.addConfig(addIt->first.c_str(), addIt->second.bulbId);
        ++addIt;
      }
      else {
        state = REMOVING_OLD;
      }
    }

    if (state == REMOVING_OLD) {
      if (removeIt != removeEnd) {
        HomeAssistantDiscoveryClient discoveryClient(settings, mqttClient);
        discoveryClient.removeConfig(removeIt->second);
        ++removeIt;
      }
      else {
        settings.deletedGroupIdAliases.clear();
        state = IDLE;
        // Republish all MQTT state after discovery
        if (bulbStateUpdater != NULL) {
          bulbStateUpdater->syncAll();
        }
      }
    }
  }
} discoveryPacer;

/**
 * Set up UDP servers (both v5 and v6).  Clean up old ones if necessary.
 */
void initMilightUdpServers() {
  if (!WiFi.isConnected()) {
    return;
  }

  udpServers.clear();

  for (size_t i = 0; i < settings.gatewayConfigs.size(); ++i) {
    const GatewayConfig& config = *settings.gatewayConfigs[i];

    std::shared_ptr<MiLightUdpServer> server =
      MiLightUdpServer::fromVersion(config.protocolVersion, milightClient, config.port, config.deviceId);

    if (server == NULL) {
      DebugSerial.print(F("Error creating UDP server with protocol version: "));
      DebugSerial.println(config.protocolVersion);
    }
    else {
      udpServers.push_back(std::move(server));
      udpServers.back()->begin();
    }
  }

  if (discoveryServer) {
    delete discoveryServer;
    discoveryServer = NULL;
  }
  if (settings.discoveryPort != 0) {
    discoveryServer = new MiLightDiscoveryServer(settings);
    discoveryServer->begin();
  }
}

/**
 * Milight RF packet handler.
 *
 * Called both when a packet is sent locally, and when an intercepted packet
 * is read.
 */
void onPacketSentHandler(uint8_t* packet, const MiLightRemoteConfig& config) {
  JsonDocument buffer;
  JsonObject result = buffer.to<JsonObject>();

  BulbId bulbId = config.packetFormatter->parsePacket(packet, result);

  // set LED mode for a packet movement
  ledStatus->oneshot(settings.ledModePacket, settings.ledModePacketCount);

  if (bulbId == DEFAULT_BULB_ID) {
    DebugSerial.println(F("Skipping packet handler because packet was not decoded"));
    return;
  }

  const MiLightRemoteConfig* remoteConfig = MiLightRemoteConfig::fromType(bulbId.deviceType);

  if (remoteConfig == NULL) {
    DebugSerial.println(F("Skipping packet handler: unknown device type"));
    return;
  }

  // update state to reflect changes from this packet
  GroupState* groupState = stateStore->get(bulbId);

  // pass in previous scratch state as well
  const GroupState stateUpdates(groupState, result);

  if (groupState != NULL) {
    groupState->patch(stateUpdates);

    // Copy state before setting it to avoid group 0 re-initialization clobbering it
    stateStore->set(bulbId, stateUpdates);
  }

  if (mqttClient) {
    // Sends the state delta derived from the raw packet
    char output[200];
    serializeJson(result, output);
    mqttClient->sendUpdate(*remoteConfig, bulbId.deviceId, bulbId.groupId, output);

    // Sends the entire state
    if (groupState != NULL) {
      bulbStateUpdater->enqueueUpdate(bulbId, *groupState);
    }
  }

  httpServer->handlePacketSent(packet, *remoteConfig, bulbId, result);
}

/**
 * Listen for packets on one radio config.  Cycles through all configs as its
 * called.
 */
void handleListen() {
  // Do not handle listens while there are packets enqueued to be sent
  // Doing so causes the radio module to need to be reinitialized inbetween
  // repeats, which slows things down.
  if (!settings.listenRepeats || packetSender->isSending()) {
    return;
  }

  std::shared_ptr<MiLightRadio> radio = radios->switchRadio(currentRadioType++ % radios->getNumRadios());

  for (size_t i = 0; i < settings.listenRepeats; i++) {
    if (radios->available()) {
      uint8_t readPacket[MILIGHT_MAX_PACKET_LENGTH];
      size_t packetLen = radios->read(readPacket);

      const MiLightRemoteConfig* remoteConfig =
        MiLightRemoteConfig::fromReceivedPacket(radio->config(), readPacket, packetLen);

      if (remoteConfig == NULL) {
        // This can happen under normal circumstances, so not an error condition
#  ifdef DEBUG_PRINTF
        DebugSerial.println(F("WARNING: Couldn't find remote for received packet"));
#  endif
        return;
      }

      // update state to reflect this packet
      onPacketSentHandler(readPacket, *remoteConfig);
    }
  }
}

/**
 * Called when MqttClient#update is first being processed.  Stop sending updates
 * and aggregate state changes until the update is finished.
 */
void onUpdateBegin() {
  if (bulbStateUpdater) {
    bulbStateUpdater->disable();
  }
}

/**
 * Called when MqttClient#update is finished processing.  Re-enable state
 * updates, which will flush accumulated state changes.
 */
void onUpdateEnd() {
  if (bulbStateUpdater) {
    bulbStateUpdater->enable();
  }
}

/**
 * Apply what's in the Settings object.
 */
void applySettings() {
  if (milightClient) {
    delete milightClient;
  }
  if (mqttClient) {
    delete mqttClient;
    delete bulbStateUpdater;

    mqttClient = NULL;
    bulbStateUpdater = NULL;
  }
  if (stateStore) {
    delete stateStore;
  }
  if (packetSender) {
    delete packetSender;
  }
  if (radios) {
    delete radios;
  }

  transitions.setDefaultPeriod(settings.defaultTransitionPeriod);

  radioFactory = MiLightRadioFactory::fromSettings(settings);

  if (radioFactory == NULL) {
    DebugSerial.println(F("ERROR: unable to construct radio factory"));
  }

  stateStore = new GroupStateStore(MILIGHT_MAX_STATE_ITEMS, settings.stateFlushInterval);

  radios = new RadioSwitchboard(radioFactory, stateStore, settings);
  packetSender = new PacketSender(*radios, settings, onPacketSentHandler);

  milightClient = new MiLightClient(*radios, *packetSender, stateStore, settings, transitions);
  milightClient->onUpdateBegin(onUpdateBegin);
  milightClient->onUpdateEnd(onUpdateEnd);

  if (settings.mqttServer().length() > 0) {
    mqttClient = new MqttClient(settings, milightClient);
    mqttClient->begin();
    mqttClient->onConnect([&]() {
      discoveryPacer.begin();
      // Re-publish all cached bulb state so MQTT reflects reality after disconnect
      if (bulbStateUpdater) {
        bulbStateUpdater->syncAll();
      }
    });

    bulbStateUpdater = new BulbStateUpdater(settings, *mqttClient, *stateStore);
    if (httpServer != NULL) {
      httpServer->setBulbStateUpdater(bulbStateUpdater);
    }
  }

  initMilightUdpServers();

  // update LED pin and operating mode
  if (ledStatus) {
    ledStatus->changePin(settings.ledPin);
    ledStatus->continuous(settings.ledModeOperating);
  }

#  ifdef ESP8266
  WiFi.hostname(settings.hostname);
#  elif defined(ESP32)
  WiFi.setHostname(settings.hostname.c_str());
#  endif
#  ifdef ESP8266
  WiFiPhyMode_t wifiPhyMode;
  switch (settings.wifiMode) {
    case WifiMode::B:
      wifiPhyMode = WIFI_PHY_MODE_11B;
      break;
    case WifiMode::G:
      wifiPhyMode = WIFI_PHY_MODE_11G;
      break;
    default:
    case WifiMode::N:
      wifiPhyMode = WIFI_PHY_MODE_11N;
      break;
  }
  WiFi.setPhyMode(wifiPhyMode);
#  elif defined(ESP32)
  switch (settings.wifiMode) {
    case WifiMode::B:
      esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);
      break;
    case WifiMode::G:
      esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11G);
      break;
    default:
    case WifiMode::N:
      esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11N);
      break;
  }
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
#  endif
}

/**
 *
 */
bool shouldRestart() {
  if (!settings.isAutoRestartEnabled()) {
    return false;
  }

  // Use unsigned long to prevent overflow in multiplication
  unsigned long periodMs = (unsigned long)settings.getAutoRestartPeriod() * 60UL * 1000UL;
  return millis() >= periodMs;
}

void aboutHandler(JsonDocument& json) {
  JsonObject mqtt = json[FPSTR("mqtt")].to<JsonObject>();
  mqtt[FPSTR("configured")] = (mqttClient != nullptr);

  if (mqttClient) {
    mqtt[FPSTR("connected")] = mqttClient->isConnected();
    mqtt[FPSTR("status")] = mqttClient->getConnectionStatusString();
  }
}

// Called when a group is deleted via the REST API.  Will publish an empty message to
// the MQTT topic to delete retained state
void onGroupDeleted(const BulbId& id) {
  if (mqttClient != NULL) {
    const MiLightRemoteConfig* config = MiLightRemoteConfig::fromType(id.deviceType);
    if (config == NULL) {
      return;
    }
    mqttClient->sendState(*config, id.deviceId, id.groupId, "");
  }
}

void onTelnetInput(String input) {
  input.trim();
  if (input == "heap") {
    telnet.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  }
  else if (input == "status") {
    String about = AboutHelper::generateAboutString(false);
    telnet.println(about);
  }
  else if (input == "uptime") {
    telnet.printf("Uptime: %lu ms\n", millis());
  }
  else if (input == "sync") {
    if (bulbStateUpdater != NULL) {
      bulbStateUpdater->syncAll();
      telnet.println(F("MQTT state sync triggered"));
    }
    else {
      telnet.println(F("MQTT not configured"));
    }
  }
  else if (input == "help") {
    telnet.println(F("Commands: heap, status, uptime, sync, help"));
  }
  else {
    telnet.println(F("Unknown command. Type 'help' for list."));
  }
}

bool initialized = false;
void postConnectSetup() {
  if (initialized)
    return;
  initialized = true;

  delete wifiManager;
  wifiManager = NULL;

  MDNS.addService("http", "tcp", 80);

  SSDP.setSchemaURL("description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName("ESP8266 MiLight Gateway");
  SSDP.setSerialNumber(getESPId());
  SSDP.setURL("/");
  SSDP.setDeviceType("upnp:rootdevice");
  SSDP.begin();

  httpServer = new MiLightHttpServer(settings, milightClient, stateStore, packetSender, radios, transitions);
  httpServer->onSettingsSaved([]() {
    bool needsRestart = wifiSettingsChanged();
    applySettings();
    snapshotWifiSettings();
    if (needsRestart) {
      DebugSerial.println(F("WiFi settings changed. Restarting..."));
      delay(1000);
      ESP.restart();
    }
  });
  httpServer->onGroupDeleted(onGroupDeleted);
  httpServer->onAbout(aboutHandler);
  httpServer->on("/description.xml", HTTP_GET, []() { SSDP.schema(httpServer->client()); });
  httpServer->begin();

  if (bulbStateUpdater != NULL) {
    httpServer->setBulbStateUpdater(bulbStateUpdater);
  }

  telnet.onInputReceived(onTelnetInput);
  telnet.begin(23);
  DebugSerial.setTelnet(&telnet);

  transitions.addListener([](const BulbId& bulbId, GroupStateField field, uint16_t value) {
    JsonDocument buffer;

    const char* fieldName = GroupStateFieldHelpers::getFieldName(field);
    buffer[fieldName] = value;

    milightClient->prepare(bulbId.deviceType, bulbId.deviceId, bulbId.groupId);
    milightClient->update(buffer.as<JsonObject>());
  });

  initMilightUdpServers();

  DebugSerial.printf("Setup complete (version %s)\n", QUOTE(MILIGHT_HUB_VERSION));
}

/**
 * Apply static IP configuration if wifi_static_ip is set.
 * Call before each WiFi.begin() attempt.
 */
void applyStaticIPConfig() {
  if (settings.wifiStaticIP.length() > 0) {
    IPAddress ip, gw, subnet, dns;
    ip.fromString(settings.wifiStaticIP);
    subnet.fromString(settings.wifiStaticIPNetmask);
    gw.fromString(settings.wifiStaticIPGateway);

    if (settings.wifiDns.length() > 0) {
      dns.fromString(settings.wifiDns);
      WiFi.config(ip, gw, subnet, dns);
    }
    else {
      WiFi.config(ip, gw, subnet);
    }
  }
}

/**
 * Try connecting to a WiFi network. Returns true if connected.
 * Blocks for up to timeoutMs milliseconds.
 */
bool tryConnect(const String& ssid, const String& password, unsigned long timeoutMs = 20000) {
  if (ssid.length() == 0)
    return false;

  DebugSerial.printf("Trying WiFi: %s\n", ssid.c_str());
  applyStaticIPConfig();
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(100);
    ledStatus->handle();
  }

  if (WiFi.status() == WL_CONNECTED) {
    DebugSerial.printf("Connected to %s (IP: %s)\n", ssid.c_str(), WiFi.localIP().toString().c_str());
    return true;
  }

  DebugSerial.printf("Failed to connect to %s\n", ssid.c_str());
  WiFi.disconnect();
  return false;
}


void setup() {
  DebugSerial.begin(9600);

  // load up our persistent settings from the file system
  if (!ProjectFS.begin()) {
    DebugSerial.println(F("Failed to mount file system, formatting..."));
    ProjectFS.format();
    ProjectFS.begin();
  }

  Settings::load(settings);
  ESPMH_SETUP_WIFI(settings);
  applySettings();

  // set up the LED status for wifi configuration
  ledStatus = new LEDStatus(settings.ledPin);
  ledStatus->continuous(settings.ledModeWifiConfig);

  // start up the wifi manager
  if (!MDNS.begin("milight-hub")) {
    DebugSerial.println(F("Error setting up MDNS responder"));
  }

  // Attempt settings-driven WiFi connection
  bool connected = false;

  if (settings.wifiSsid.length() > 0 || settings.wifiSsidSecondary.length() > 0) {
    // Dual-WiFi ordered failover: primary -> secondary -> retry or portal
    while (!connected) {
      connected = tryConnect(settings.wifiSsid, settings.wifiPassword);
      if (!connected) {
        connected = tryConnect(settings.wifiSsidSecondary, settings.wifiPasswordSecondary);
      }
      if (!connected) {
        if (settings.wifiPortalOnFail) {
          break; // fall through to portal
        }
        DebugSerial.println(F("Both WiFi networks failed. Retrying..."));
        delay(5000);
      }
    }
  }

  if (!connected) {
    // No SSIDs configured or both failed with portal enabled — use WiFiManager captive portal
    wifiManager = new WiFiManager();
    wifiManager->setBreakAfterConfig(true);
    wifiManager->setConnectTimeout(20);
    wifiManager->setConnectRetries(5);

    // Static IP for portal path
    if (settings.wifiStaticIP.length() > 0) {
      IPAddress _ip, _subnet, _gw;
      _ip.fromString(settings.wifiStaticIP);
      _subnet.fromString(settings.wifiStaticIPNetmask);
      _gw.fromString(settings.wifiStaticIPGateway);
      wifiManager->setSTAStaticIPConfig(_ip, _gw, _subnet);
    }

    wifiManager->setConfigPortalTimeout(180);
    wifiManager->setConfigPortalTimeoutCallback([]() {
      ledStatus->continuous(settings.ledModeWifiFailed);
      DebugSerial.println(F("Wifi config portal timed out.  Restarting..."));
      delay(10000);
      ESP.restart();
    });

    String ssid = "ESP" + String(getESPId());
    connected = wifiManager->autoConnect(ssid.c_str(), "milightHub");
  }

  if (connected) {
    ledStatus->continuous(settings.ledModeOperating);
    DebugSerial.println(F("Wifi connected successfully"));
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // If connected via portal, save credentials to settings so future boots
    // use the fast tryConnect path instead of falling through to portal again
    if (settings.wifiSsid.length() == 0 && WiFi.SSID().length() > 0) {
      DebugSerial.println(F("Saving portal credentials to settings"));
      settings.wifiSsid = WiFi.SSID();
      settings.wifiPassword = WiFi.psk();
      settings.save();
    }

    postConnectSetup();
  }

  snapshotWifiSettings();
}

size_t i = 0;

void loop() {
  // update LED with status
  ledStatus->handle();

  if (shouldRestart()) {
    DebugSerial.println(F("Auto-restart triggered. Restarting..."));
    ESP.restart();
  }

  if (wifiManager) {
    wifiManager->process();
  }

  if (WiFi.getMode() == WIFI_STA && WiFi.isConnected()) {
    postConnectSetup();

    httpServer->handleClient();
    if (mqttClient) {
      mqttClient->handleClient();
      bulbStateUpdater->loop();
      discoveryPacer.loop();
    }

    for (auto& udpServer : udpServers) {
      udpServer->handleClient();
    }

    if (discoveryServer) {
      discoveryServer->handleClient();
    }

    handleListen();

#  ifdef ESP8266
    MDNS.update();
#  endif

    stateStore->limitedFlush();
    packetSender->loop();

    transitions.loop();

    telnet.loop();
  }
  else if (initialized && WiFi.getMode() == WIFI_STA && !WiFi.isConnected()) {
    static unsigned long lastReconnectAttempt = 0;
    static uint8_t retryCount = 0;
    static bool onSecondary = false;

    if (millis() - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = millis();

      if (retryCount < 3) {
        // Retry current SSID
        DebugSerial.println(F("WiFi disconnected. Retrying current SSID..."));
        WiFi.reconnect();
        retryCount++;
      }
      else {
        // Switch to the other SSID
        retryCount = 0;
        onSecondary = !onSecondary;
        const String& ssid = onSecondary ? settings.wifiSsidSecondary : settings.wifiSsid;
        const String& pass = onSecondary ? settings.wifiPasswordSecondary : settings.wifiPassword;

        if (ssid.length() > 0) {
          DebugSerial.printf("Failing over to %s\n", ssid.c_str());
          applyStaticIPConfig();
          WiFi.begin(ssid.c_str(), pass.c_str());
        }
        else {
          // Other SSID not configured, flip back
          onSecondary = !onSecondary;
          WiFi.reconnect();
        }
      }
    }
  }

  // Heap monitoring
  static unsigned long lastHeapCheck = 0;
  if (millis() - lastHeapCheck > 60000) {
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 4096) {
      DebugSerial.print(F("CRITICAL: Free heap "));
      DebugSerial.print(freeHeap);
      DebugSerial.println(F(" bytes, restarting"));
      ESP.restart();
    }
    else if (freeHeap < 8192) {
      DebugSerial.print(F("WARNING: Low free heap: "));
      DebugSerial.print(freeHeap);
      DebugSerial.println(F(" bytes"));
    }
    lastHeapCheck = millis();
  }
}

#endif