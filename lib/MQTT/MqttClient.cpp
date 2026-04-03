#include <stddef.h>
#include <MqttClient.h>
#include <TokenIterator.h>
#include <UrlTokenBindings.h>
#include <IntParsing.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <MiLightRadioConfig.h>
#include <AboutHelper.h>
#include <DebugSerial.h>


const std::map<int, const __FlashStringHelper*> MQTT_STATUS_STRINGS = {
  {MQTT_CONNECTION_TIMEOUT, FPSTR("Connection Timeout")},
  {MQTT_CONNECTION_LOST, FPSTR("Connection Lost")},
  {MQTT_CONNECT_FAILED, FPSTR("Connect Failed")},
  {MQTT_DISCONNECTED, FPSTR("Disconnected")},
  {MQTT_CONNECTED, FPSTR("Connected")},
  {MQTT_CONNECT_BAD_PROTOCOL, FPSTR("Connect Bad Protocol")},
  {MQTT_CONNECT_BAD_CLIENT_ID, FPSTR("Connect Bad Client ID")},
  {MQTT_CONNECT_UNAVAILABLE, FPSTR("Connect Unavailable")},
  {MQTT_CONNECT_BAD_CREDENTIALS, FPSTR("Connect Bad Credentials")},
  {MQTT_CONNECT_UNAUTHORIZED, FPSTR("Connect Unauthorized")}
};

static const char* STATUS_CONNECTED = "connected";
static const char* STATUS_DISCONNECTED = "disconnected_clean";
static const char* STATUS_LWT_DISCONNECTED = "disconnected_unclean";

MqttClient::MqttClient(Settings& settings, MiLightClient*& milightClient)
  : mqttClient(tcpClient),
    milightClient(milightClient),
    settings(settings),
    lastConnectAttempt(0),
    connected(false)
{
  String strDomain = settings.mqttServer();
  this->domain = new char[strDomain.length() + 1];
  strcpy(this->domain, strDomain.c_str());
}

MqttClient::~MqttClient() {
  if (mqttClient.connected()) {
    String aboutStr = generateConnectionStatusMessage(STATUS_DISCONNECTED);
    mqttClient.publish(settings.mqttClientStatusTopic.c_str(), aboutStr.c_str(), true);
    mqttClient.disconnect();
  }
  delete[] this->domain;
}

void MqttClient::onConnect(OnConnectFn fn) {
  this->onConnectFn = fn;
}

void MqttClient::begin() {
#ifdef MQTT_DEBUG
  printf(
    "MqttClient - Connecting to: %s\nparsed:%s:%u\n",
    settings._mqttServer.c_str(),
    settings.mqttServer().c_str(),
    settings.mqttPort()
  );
#endif

  mqttClient.setServer(this->domain, settings.mqttPort());
  mqttClient.setSocketTimeout(2);
  mqttClient.setCallback(
    [this](char* topic, byte* payload, int length) {
      this->publishCallback(topic, payload, length);
    }
  );
  reconnect();
}

bool MqttClient::connect() {
  char nameBuffer[30];
  sprintf_P(nameBuffer, PSTR("milight-hub-%u"), getESPId());
  String lwtMessage = generateConnectionStatusMessage(STATUS_LWT_DISCONNECTED);

#ifdef MQTT_DEBUG
    DebugSerial.println(F("MqttClient - connecting using name"));
    DebugSerial.println(nameBuffer);
#endif

  if (settings.mqttUsername.length() > 0 && settings.mqttClientStatusTopic.length() > 0) {
    return mqttClient.connect(
      nameBuffer,
      settings.mqttUsername.c_str(),
      settings.mqttPassword.c_str(),
      settings.mqttClientStatusTopic.c_str(),
      2,
      true,
      lwtMessage.c_str()
    );
  } else if (settings.mqttUsername.length() > 0) {
    return mqttClient.connect(
      nameBuffer,
      settings.mqttUsername.c_str(),
      settings.mqttPassword.c_str()
    );
  } else if (settings.mqttClientStatusTopic.length() > 0) {
    return mqttClient.connect(
      nameBuffer,
      settings.mqttClientStatusTopic.c_str(),
      2,
      true,
      lwtMessage.c_str()
    );
  } else {
    return mqttClient.connect(nameBuffer);
  }
}

void MqttClient::sendBirthMessage() {
  if (settings.mqttClientStatusTopic.length() > 0) {
    String aboutStr = generateConnectionStatusMessage(STATUS_CONNECTED);
    mqttClient.publish(settings.mqttClientStatusTopic.c_str(), aboutStr.c_str(), true);
  }
}

void MqttClient::reconnect() {
  if (lastConnectAttempt > 0 && (millis() - lastConnectAttempt) < MQTT_CONNECTION_ATTEMPT_FREQUENCY) {
    return;
  }

  if (! mqttClient.connected()) {
    if (connect()) {
      subscribe();
      sendBirthMessage();

#ifdef MQTT_DEBUG
      DebugSerial.println(F("MqttClient - Successfully connected to MQTT server"));
#endif
    } else {
      DebugSerial.print(F("ERROR: Failed to connect to MQTT server rc="));
      DebugSerial.println(mqttClient.state());
    }
  }

  lastConnectAttempt = millis();
}

void MqttClient::handleClient() {
  reconnect();
  mqttClient.loop();

  if (!connected && mqttClient.connected()) {
    this->connected = true;
    if (this->onConnectFn) {
      this->onConnectFn();
    }
  } else if (!mqttClient.connected()) {
    this->connected = false;
  }
}

void MqttClient::sendUpdate(const MiLightRemoteConfig& remoteConfig, uint16_t deviceId, uint16_t groupId, const char* update) {
  publish(settings.mqttUpdateTopicPattern, remoteConfig, deviceId, groupId, update, false);
}

void MqttClient::sendState(const MiLightRemoteConfig& remoteConfig, uint16_t deviceId, uint16_t groupId, const char* update) {
  publish(settings.mqttStateTopicPattern, remoteConfig, deviceId, groupId, update, true);
}

void MqttClient::subscribe() {
  String topic = settings.mqttTopicPattern;

  topic.replace(":device_id", "+");
  topic.replace(":hex_device_id", "+");
  topic.replace(":dec_device_id", "+");
  topic.replace(":group_id", "+");
  topic.replace(":device_type", "+");
  topic.replace(":device_alias", "+");

#ifdef MQTT_DEBUG
  printf("MqttClient - subscribing to topic: %s\n", topic.c_str());
#endif

  bool success = mqttClient.subscribe(topic.c_str(), 1);
  if (!success) {
    DebugSerial.println(F("ERROR: MQTT subscribe failed"));
  }
}

void MqttClient::send(const char* topic, const char* message, const bool retain) {
  size_t len = strlen(message);
  size_t topicLen = strlen(topic);

  if ((topicLen + len + 10) < MQTT_MAX_PACKET_SIZE ) {
    mqttClient.publish(topic, message, retain);
  } else {
    const uint8_t* messageBuffer = reinterpret_cast<const uint8_t*>(message);

#ifdef MQTT_DEBUG
    DebugSerial.printf("Printing message in parts because it's too large for the packet buffer (%d bytes)", len);
#endif

    if (!mqttClient.beginPublish(topic, len, retain)) {
      DebugSerial.println(F("MqttClient - beginPublish failed"));
      return;
    }

    for (size_t i = 0; i < len; i += MQTT_PACKET_CHUNK_SIZE) {
      size_t toWrite = std::min(static_cast<size_t>(MQTT_PACKET_CHUNK_SIZE), len - i);
      size_t written = mqttClient.write(messageBuffer+i, toWrite);
      if (written != toWrite) {
        DebugSerial.println(F("MqttClient - write failed mid-publish"));
        break;
      }
#ifdef MQTT_DEBUG
      DebugSerial.printf("  Wrote %d bytes\n", toWrite);
#endif
    }

    mqttClient.endPublish();
  }
}

void MqttClient::publish(
  const String& _topic,
  const MiLightRemoteConfig &remoteConfig,
  uint16_t deviceId,
  uint16_t groupId,
  const char* message,
  const bool _retain
) {
  if (_topic.length() == 0) {
    return;
  }

  BulbId bulbId(deviceId, groupId, remoteConfig.type);
  String topic = bindTopicString(_topic, bulbId);
  const bool retain = _retain && this->settings.mqttRetain;

#ifdef MQTT_DEBUG
  printf("MqttClient - publishing update to %s\n", topic.c_str());
#endif

  send(topic.c_str(), message, retain);
}

void MqttClient::publishCallback(char* topic, byte* payload, int length) {
  uint16_t deviceId = 0;
  uint8_t groupId = 0;
  const MiLightRemoteConfig* config = &FUT092Config;
  const int MAX_MQTT_PAYLOAD = 700;
  if (length > MAX_MQTT_PAYLOAD) {
    DebugSerial.printf("MqttClient - payload too large (%d bytes), ignoring\n", length);
    return;
  }
  char cstrPayload[MAX_MQTT_PAYLOAD + 1];
  memcpy(cstrPayload, payload, length);
  cstrPayload[length] = 0;

#ifdef MQTT_DEBUG
  printf("MqttClient - Got message on topic: %s\n%s\n", topic, cstrPayload);
#endif

  auto patternIterator = std::make_shared<TokenIterator>(settings.mqttTopicPattern.c_str(), settings.mqttTopicPattern.length(), '/');
  auto topicIterator = std::make_shared<TokenIterator>(topic, strlen(topic), '/');
  UrlTokenBindings tokenBindings(patternIterator, topicIterator);

  if (tokenBindings.hasBinding("device_alias")) {
    String alias = tokenBindings.get("device_alias");
    auto itr = settings.groupIdAliases.find(alias);

    if (itr == settings.groupIdAliases.end()) {
      DebugSerial.printf("MqttClient - WARNING: could not find device alias: `%s'. Ignoring packet.\n", alias.c_str());
      return;
    } else {
      BulbId bulbId = itr->second.bulbId;

      deviceId = bulbId.deviceId;
      config = MiLightRemoteConfig::fromType(bulbId.deviceType);
      groupId = bulbId.groupId;
    }
  } else {
    if (tokenBindings.hasBinding(GroupStateFieldNames::DEVICE_ID)) {
      deviceId = parseInt<uint16_t>(tokenBindings.get(GroupStateFieldNames::DEVICE_ID));
    } else if (tokenBindings.hasBinding("hex_device_id")) {
      deviceId = parseInt<uint16_t>(tokenBindings.get("hex_device_id"));
    } else if (tokenBindings.hasBinding("dec_device_id")) {
      deviceId = parseInt<uint16_t>(tokenBindings.get("dec_device_id"));
    }

    if (tokenBindings.hasBinding(GroupStateFieldNames::GROUP_ID)) {
      groupId = parseInt<uint16_t>(tokenBindings.get(GroupStateFieldNames::GROUP_ID));
    }

    if (tokenBindings.hasBinding(GroupStateFieldNames::DEVICE_TYPE)) {
      config = MiLightRemoteConfig::fromType(tokenBindings.get(GroupStateFieldNames::DEVICE_TYPE));
    } else {
      DebugSerial.println(F("MqttClient - WARNING: could not find device_type token.  Defaulting to FUT092.\n"));
    }
  }

  if (config == NULL) {
    DebugSerial.println(F("MqttClient - ERROR: unknown device_type specified"));
    return;
  }

  JsonDocument buffer;
  DeserializationError err = deserializeJson(buffer, cstrPayload);
  if (err) {
    DebugSerial.printf("MqttClient - JSON parse error: %s\n", err.c_str());
    return;
  }
  JsonObject obj = buffer.as<JsonObject>();

#ifdef MQTT_DEBUG
  printf("MqttClient - device %04X, group %u\n", deviceId, groupId);
#endif

  milightClient->prepare(config, deviceId, groupId);
  milightClient->update(obj);
}

String MqttClient::bindTopicString(const String& topicPattern, const BulbId& bulbId) {
  // Pre-format substitution values on stack (no heap allocs)
  char hexDeviceId[7];
  sprintf_P(hexDeviceId, PSTR("0x%X"), bulbId.deviceId);

  char decDeviceId[6];
  sprintf(decDeviceId, "%u", bulbId.deviceId);

  char groupIdStr[4];
  sprintf(groupIdStr, "%u", bulbId.groupId);

  const char* deviceType = MiLightRemoteTypeHelpers::remoteTypeToString(bulbId.deviceType);

  const char* alias = "__unnamed_group";
  auto it = settings.findAlias(bulbId.deviceType, bulbId.deviceId, bulbId.groupId);
  if (it != settings.groupIdAliases.end()) {
    alias = it->first.c_str();
  }

  // Replace longer tokens first to avoid substring matches
  String boundTopic = topicPattern;
  boundTopic.replace(":device_alias", alias);
  boundTopic.replace(":hex_device_id", hexDeviceId);
  boundTopic.replace(":dec_device_id", decDeviceId);
  boundTopic.replace(":device_id", hexDeviceId);
  boundTopic.replace(":device_type", deviceType);
  boundTopic.replace(":group_id", groupIdStr);

  return boundTopic;
}

String MqttClient::generateConnectionStatusMessage(const char* connectionStatus) {
  if (settings.simpleMqttClientStatus) {
    // Don't expand disconnect type for simple status
    if (0 == strcmp(connectionStatus, STATUS_CONNECTED)) {
      return connectionStatus;
    } else {
      return "disconnected";
    }
  } else {
    JsonDocument json;
    json[GroupStateFieldNames::STATUS] = connectionStatus;

    // Fill other fields
    AboutHelper::generateAboutObject(json, true);

    String response;
    serializeJson(json, response);

    return response;
  }
}

bool MqttClient::isConnected() {
  return this->mqttClient.connected();
}

MqttConnectionStatus MqttClient::getConnectionStatus() {
  return static_cast<MqttConnectionStatus>(this->mqttClient.state());
}

const __FlashStringHelper* MqttClient::getConnectionStatusString() {
  auto it = MQTT_STATUS_STRINGS.find(this->mqttClient.state());
  if (it != MQTT_STATUS_STRINGS.end()) {
    return it->second;
  }
  return F("Unknown");
}
