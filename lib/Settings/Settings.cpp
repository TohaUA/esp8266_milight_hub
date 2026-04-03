#include <Settings.h>
#include <ArduinoJson.h>
#include <IntParsing.h>
#include <algorithm>
#include <JsonHelpers.h>
#include <GroupAlias.h>
#include <ProjectFS.h>
#include <StreamUtils.h>
#include <DebugSerial.h>

const std::vector<GroupStateField> DEFAULT_GROUP_STATE_FIELDS({
  GroupStateField::STATE,
  GroupStateField::BRIGHTNESS,
  GroupStateField::COMPUTED_COLOR,
  GroupStateField::MODE,
  GroupStateField::COLOR_TEMP,
  GroupStateField::COLOR_MODE
});

#define PORT_POSITION(s) ( s.indexOf(':') )

GatewayConfig::GatewayConfig(uint16_t deviceId, uint16_t port, uint8_t protocolVersion)
  : deviceId(deviceId)
  , port(port)
  , protocolVersion(protocolVersion)
{ }

bool Settings::isAuthenticationEnabled() const {
  return adminUsername.length() > 0 && adminPassword.length() > 0;
}

const String& Settings::getUsername() const {
  return adminUsername;
}

const String& Settings::getPassword() const {
  return adminPassword;
}

bool Settings::isAutoRestartEnabled() {
  return _autoRestartPeriod > 0;
}

size_t Settings::getAutoRestartPeriod() {
  if (_autoRestartPeriod == 0) {
    return 0;
  }

  return std::max(_autoRestartPeriod, static_cast<size_t>(MINIMUM_RESTART_PERIOD));
}

void Settings::updateDeviceIds(JsonArray arr) {
  this->deviceIds.clear();

  for (size_t i = 0; i < arr.size(); ++i) {
    this->deviceIds.push_back(arr[i]);
  }
}

void Settings::updateGatewayConfigs(JsonArray arr) {
  gatewayConfigs.clear();

  for (size_t i = 0; i < arr.size(); i++) {
    JsonArray params = arr[i];

    if (params.size() == 3) {
      std::shared_ptr<GatewayConfig> ptr = std::make_shared<GatewayConfig>(parseInt<uint16_t>(params[0]), params[1], params[2]);
      gatewayConfigs.push_back(std::move(ptr));
    } else {
      DebugSerial.print(F("Settings - skipped parsing gateway ports settings for element #"));
      DebugSerial.println(i);
    }
  }
}

String Settings::validateStringLen(JsonObject obj, const __FlashStringHelper* key, size_t maxLen) {
  if (!obj.containsKey(key)) return String();
  if (!obj[key].is<const char*>()) {
    char buf[64];
    snprintf_P(buf, sizeof(buf), PSTR("%s: must be a string"), reinterpret_cast<const char*>(key));
    return String(buf);
  }
  const char* val = obj[key].as<const char*>();
  if (strlen(val) > maxLen) {
    char buf[80];
    snprintf_P(buf, sizeof(buf), PSTR("%s: max length is %d"), reinterpret_cast<const char*>(key), (int)maxLen);
    return String(buf);
  }
  return String();
}

String Settings::validateRange(JsonObject obj, const __FlashStringHelper* key, long min, long max) {
  if (!obj.containsKey(key)) return String();
  if (!obj[key].is<int>() && !obj[key].is<long>() && !obj[key].is<unsigned int>()) {
    char buf[64];
    snprintf_P(buf, sizeof(buf), PSTR("%s: must be a number"), reinterpret_cast<const char*>(key));
    return String(buf);
  }
  long val = obj[key].as<long>();
  if (val < min || val > max) {
    char buf[80];
    snprintf_P(buf, sizeof(buf), PSTR("%s: must be between %ld and %ld"), reinterpret_cast<const char*>(key), min, max);
    return String(buf);
  }
  return String();
}

String Settings::validateEnum(JsonObject obj, const __FlashStringHelper* key, const char* const validValues[], size_t numValues) {
  if (!obj.containsKey(key)) return String();
  if (!obj[key].is<const char*>()) {
    char buf[64];
    snprintf_P(buf, sizeof(buf), PSTR("%s: must be a string"), reinterpret_cast<const char*>(key));
    return String(buf);
  }
  const char* val = obj[key].as<const char*>();
  for (size_t i = 0; i < numValues; i++) {
    if (strcasecmp(val, validValues[i]) == 0) return String();
  }
  char buf[80];
  snprintf_P(buf, sizeof(buf), PSTR("%s: invalid value '%s'"), reinterpret_cast<const char*>(key), val);
  return String(buf);
}

String Settings::validate(JsonObject obj) const {
  String err;

  // --- String lengths ---
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::ADMIN_USERNAME), 32)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::ADMIN_PASSWORD), 64)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::MQTT_SERVER), 128)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::MQTT_USERNAME), 64)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::MQTT_PASSWORD), 128)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::MQTT_TOPIC_PATTERN), 128)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::MQTT_UPDATE_TOPIC_PATTERN), 128)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::MQTT_STATE_TOPIC_PATTERN), 128)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::MQTT_CLIENT_STATUS_TOPIC), 128)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::HOME_ASSISTANT_DISCOVERY_PREFIX), 128)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::WIFI_STATIC_IP), 15)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::WIFI_STATIC_IP_GATEWAY), 15)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::WIFI_STATIC_IP_NETMASK), 15)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::WIFI_SSID), 32)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::WIFI_PASSWORD), 63)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::WIFI_SSID_SECONDARY), 32)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::WIFI_PASSWORD_SECONDARY), 63)).length()) return err;
  if ((err = validateStringLen(obj, FPSTR(SettingsKeys::WIFI_DNS), 15)).length()) return err;

  // Secondary WiFi requires primary
  if (obj.containsKey(FPSTR(SettingsKeys::WIFI_SSID_SECONDARY))) {
    const char* secondary = obj[FPSTR(SettingsKeys::WIFI_SSID_SECONDARY)].as<const char*>();
    if (secondary && strlen(secondary) > 0) {
      // Check if primary is being set in same request, or already configured
      const char* primary = nullptr;
      if (obj.containsKey(FPSTR(SettingsKeys::WIFI_SSID))) {
        primary = obj[FPSTR(SettingsKeys::WIFI_SSID)].as<const char*>();
      }
      if ((!primary || strlen(primary) == 0) && wifiSsid.length() == 0) {
        return F("wifi_ssid_secondary: primary wifi_ssid must be set first");
      }
    }
  }

  // hostname: 1-63 chars
  if (obj.containsKey(FPSTR(SettingsKeys::HOSTNAME))) {
    if ((err = validateStringLen(obj, FPSTR(SettingsKeys::HOSTNAME), 63)).length()) return err;
    const char* h = obj[FPSTR(SettingsKeys::HOSTNAME)].as<const char*>();
    if (h && strlen(h) == 0) return F("hostname: must not be empty");
  }

  // --- Enums ---
  static const char* const radioTypes[] = {"nRF24", "LT8900"};
  if ((err = validateEnum(obj, FPSTR(SettingsKeys::RADIO_INTERFACE_TYPE), radioTypes, 2)).length()) return err;

  static const char* const wifiModes[] = {"b", "g", "n"};
  if ((err = validateEnum(obj, FPSTR(SettingsKeys::WIFI_MODE), wifiModes, 3)).length()) return err;

  static const char* const rf24PowerLevels[] = {"MIN", "LOW", "HIGH", "MAX"};
  if ((err = validateEnum(obj, FPSTR(SettingsKeys::RF24_POWER_LEVEL), rf24PowerLevels, 4)).length()) return err;

  static const char* const rf24ChannelValues[] = {"LOW", "MID", "HIGH"};
  if ((err = validateEnum(obj, FPSTR(SettingsKeys::RF24_LISTEN_CHANNEL), rf24ChannelValues, 3)).length()) return err;

  static const char* const ledModes[] = {"Off", "Slow toggle", "Fast toggle", "Slow blip", "Fast blip", "Flicker", "On"};
  if ((err = validateEnum(obj, FPSTR(SettingsKeys::LED_MODE_WIFI_CONFIG), ledModes, 7)).length()) return err;
  if ((err = validateEnum(obj, FPSTR(SettingsKeys::LED_MODE_WIFI_FAILED), ledModes, 7)).length()) return err;
  if ((err = validateEnum(obj, FPSTR(SettingsKeys::LED_MODE_OPERATING), ledModes, 7)).length()) return err;
  if ((err = validateEnum(obj, FPSTR(SettingsKeys::LED_MODE_PACKET), ledModes, 7)).length()) return err;

  // --- GPIO pins ---
  if ((err = validateRange(obj, FPSTR(SettingsKeys::CE_PIN), 0, 39)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::CSN_PIN), 0, 39)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::RESET_PIN), 0, 39)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::LED_PIN), -39, 39)).length()) return err;

  // --- Integer ranges ---
  if ((err = validateRange(obj, FPSTR(SettingsKeys::PACKET_REPEATS), 1, 1000)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::HTTP_REPEAT_FACTOR), 1, 100)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::LISTEN_REPEATS), 0, 255)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::DISCOVERY_PORT), 0, 65535)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::MQTT_STATE_RATE_LIMIT), 0, 60000)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::MQTT_DEBOUNCE_DELAY), 0, 60000)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::PACKET_REPEAT_THROTTLE_THRESHOLD), 0, 10000)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::PACKET_REPEAT_THROTTLE_SENSITIVITY), 0, 1000)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::PACKET_REPEAT_MINIMUM), 1, 1000)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::LED_MODE_PACKET_COUNT), 0, 100)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::PACKET_REPEATS_PER_LOOP), 1, 1000)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::DEFAULT_TRANSITION_PERIOD), 100, 65535)).length()) return err;
  if ((err = validateRange(obj, FPSTR(SettingsKeys::AUTO_RESTART_PERIOD), 0, 71582)).length()) return err;

  // state_flush_interval: 0 (disabled) or 100-3600000
  if (obj.containsKey(FPSTR(SettingsKeys::STATE_FLUSH_INTERVAL))) {
    long val = obj[FPSTR(SettingsKeys::STATE_FLUSH_INTERVAL)].as<long>();
    if (val != 0 && (val < 100 || val > 3600000)) {
      return F("state_flush_interval: must be 0 (disabled) or 100-3600000");
    }
  }

  // --- Arrays ---
  if (obj.containsKey(FPSTR(SettingsKeys::RF24_CHANNELS))) {
    if (!obj[FPSTR(SettingsKeys::RF24_CHANNELS)].is<JsonArray>()) {
      return F("rf24_channels: must be an array");
    }
    JsonArray arr = obj[FPSTR(SettingsKeys::RF24_CHANNELS)];
    if (arr.size() == 0 || arr.size() > 3) {
      return F("rf24_channels: must have 1-3 elements");
    }
    static const char* const validChannels[] = {"LOW", "MID", "HIGH"};
    for (size_t i = 0; i < arr.size(); i++) {
      if (!arr[i].is<const char*>()) return F("rf24_channels: elements must be strings");
      const char* ch = arr[i].as<const char*>();
      bool valid = false;
      for (size_t j = 0; j < 3; j++) {
        if (strcmp(ch, validChannels[j]) == 0) { valid = true; break; }
      }
      if (!valid) {
        char buf[64];
        snprintf_P(buf, sizeof(buf), PSTR("rf24_channels: invalid channel '%s'"), ch);
        return String(buf);
      }
    }
  }

  if (obj.containsKey(FPSTR(SettingsKeys::DEVICE_IDS))) {
    if (!obj[FPSTR(SettingsKeys::DEVICE_IDS)].is<JsonArray>()) {
      return F("device_ids: must be an array");
    }
    JsonArray arr = obj[FPSTR(SettingsKeys::DEVICE_IDS)];
    if (arr.size() > 256) {
      return F("device_ids: max 256 entries");
    }
  }

  if (obj.containsKey(FPSTR(SettingsKeys::GATEWAY_CONFIGS))) {
    if (!obj[FPSTR(SettingsKeys::GATEWAY_CONFIGS)].is<JsonArray>()) {
      return F("gateway_configs: must be an array");
    }
    JsonArray arr = obj[FPSTR(SettingsKeys::GATEWAY_CONFIGS)];
    if (arr.size() > 64) {
      return F("gateway_configs: max 64 entries");
    }
    for (size_t i = 0; i < arr.size(); i++) {
      if (!arr[i].is<JsonArray>()) return F("gateway_configs: each entry must be [device_id, port, protocol]");
      JsonArray entry = arr[i].as<JsonArray>();
      if (entry.size() != 3) return F("gateway_configs: each entry must have 3 elements");
      long port = entry[1].as<long>();
      long proto = entry[2].as<long>();
      if (port < 1 || port > 65535) return F("gateway_configs: port must be 1-65535");
      if (proto != 5 && proto != 6) return F("gateway_configs: protocol must be 5 or 6");
    }
  }

  if (obj.containsKey(FPSTR(SettingsKeys::GROUP_STATE_FIELDS))) {
    if (!obj[FPSTR(SettingsKeys::GROUP_STATE_FIELDS)].is<JsonArray>()) {
      return F("group_state_fields: must be an array");
    }
    JsonArray arr = obj[FPSTR(SettingsKeys::GROUP_STATE_FIELDS)];
    if (arr.size() > 16) {
      return F("group_state_fields: max 16 entries");
    }
    for (size_t i = 0; i < arr.size(); i++) {
      if (!arr[i].is<const char*>()) return F("group_state_fields: elements must be strings");
      if (GroupStateFieldHelpers::getFieldByName(arr[i].as<const char*>()) == GroupStateField::UNKNOWN) {
        char buf[80];
        snprintf_P(buf, sizeof(buf), PSTR("group_state_fields: unknown field '%s'"), arr[i].as<const char*>());
        return String(buf);
      }
    }
  }

  if (obj.containsKey(FPSTR(SettingsKeys::GROUP_ID_ALIASES))) {
    if (!obj[FPSTR(SettingsKeys::GROUP_ID_ALIASES)].is<JsonObject>()) {
      return F("group_id_aliases: must be an object");
    }
    JsonObject aliases = obj[FPSTR(SettingsKeys::GROUP_ID_ALIASES)];
    for (JsonPair kv : aliases) {
      if (strlen(kv.key().c_str()) > MAX_ALIAS_LEN) {
        char buf[80];
        snprintf_P(buf, sizeof(buf), PSTR("group_id_aliases: alias '%s' exceeds max length %d"), kv.key().c_str(), MAX_ALIAS_LEN);
        return String(buf);
      }
      if (!kv.value().is<JsonArray>()) {
        return F("group_id_aliases: each value must be [device_type, device_id, group_id]");
      }
      JsonArray bulbArr = kv.value().as<JsonArray>();
      if (bulbArr.size() != 3) {
        return F("group_id_aliases: each value must have 3 elements");
      }
      if (!bulbArr[0].is<const char*>()) {
        return F("group_id_aliases: device_type must be a string");
      }
      if (MiLightRemoteTypeHelpers::remoteTypeFromString(bulbArr[0].as<String>()) == REMOTE_TYPE_UNKNOWN) {
        char buf[80];
        snprintf_P(buf, sizeof(buf), PSTR("group_id_aliases: unknown device_type '%s'"), bulbArr[0].as<const char*>());
        return String(buf);
      }
      long groupId = bulbArr[2].as<long>();
      if (groupId < 0 || groupId > 8) {
        return F("group_id_aliases: group_id must be 0-8");
      }
    }
  }

  return String(); // all checks passed
}

String Settings::patch(JsonObject parsedSettings) {
  if (parsedSettings.isNull()) {
    DebugSerial.println(F("Skipping patching loaded settings.  Parsed settings was null."));
    return String();
  }

  String validationError = validate(parsedSettings);
  if (validationError.length() > 0) {
    return validationError;
  }

  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::ADMIN_USERNAME), adminUsername);
  // Skip redacted sentinel "***" so saving settings back doesn't clobber real password
  if (parsedSettings.containsKey(FPSTR(SettingsKeys::ADMIN_PASSWORD))
      && strcmp(parsedSettings[FPSTR(SettingsKeys::ADMIN_PASSWORD)].as<const char*>(), "***") != 0) {
    this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::ADMIN_PASSWORD), adminPassword);
  }
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::CE_PIN), cePin);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::CSN_PIN), csnPin);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::RESET_PIN), resetPin);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::LED_PIN), ledPin);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::PACKET_REPEATS), packetRepeats);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::HTTP_REPEAT_FACTOR), httpRepeatFactor);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::AUTO_RESTART_PERIOD), _autoRestartPeriod);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::MQTT_SERVER), _mqttServer);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::MQTT_USERNAME), mqttUsername);
  if (parsedSettings.containsKey(FPSTR(SettingsKeys::MQTT_PASSWORD))
      && strcmp(parsedSettings[FPSTR(SettingsKeys::MQTT_PASSWORD)].as<const char*>(), "***") != 0) {
    this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::MQTT_PASSWORD), mqttPassword);
  }
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::MQTT_TOPIC_PATTERN), mqttTopicPattern);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::MQTT_UPDATE_TOPIC_PATTERN), mqttUpdateTopicPattern);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::MQTT_STATE_TOPIC_PATTERN), mqttStateTopicPattern);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::MQTT_CLIENT_STATUS_TOPIC), mqttClientStatusTopic);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::SIMPLE_MQTT_CLIENT_STATUS), simpleMqttClientStatus);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::DISCOVERY_PORT), discoveryPort);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::LISTEN_REPEATS), listenRepeats);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::STATE_FLUSH_INTERVAL), stateFlushInterval);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::MQTT_STATE_RATE_LIMIT), mqttStateRateLimit);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::MQTT_DEBOUNCE_DELAY), mqttDebounceDelay);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::MQTT_RETAIN), mqttRetain);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::PACKET_REPEAT_THROTTLE_THRESHOLD), packetRepeatThrottleThreshold);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::PACKET_REPEAT_THROTTLE_SENSITIVITY), packetRepeatThrottleSensitivity);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::PACKET_REPEAT_MINIMUM), packetRepeatMinimum);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::ENABLE_AUTOMATIC_MODE_SWITCHING), enableAutomaticModeSwitching);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::LED_MODE_PACKET_COUNT), ledModePacketCount);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::HOSTNAME), hostname);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::WIFI_STATIC_IP), wifiStaticIP);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::WIFI_STATIC_IP_GATEWAY), wifiStaticIPGateway);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::WIFI_STATIC_IP_NETMASK), wifiStaticIPNetmask);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::WIFI_SSID), wifiSsid);
  if (parsedSettings.containsKey(FPSTR(SettingsKeys::WIFI_PASSWORD))
      && strcmp(parsedSettings[FPSTR(SettingsKeys::WIFI_PASSWORD)].as<const char*>(), "***") != 0) {
    this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::WIFI_PASSWORD), wifiPassword);
  }
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::WIFI_SSID_SECONDARY), wifiSsidSecondary);
  if (parsedSettings.containsKey(FPSTR(SettingsKeys::WIFI_PASSWORD_SECONDARY))
      && strcmp(parsedSettings[FPSTR(SettingsKeys::WIFI_PASSWORD_SECONDARY)].as<const char*>(), "***") != 0) {
    this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::WIFI_PASSWORD_SECONDARY), wifiPasswordSecondary);
  }
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::WIFI_DNS), wifiDns);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::WIFI_PORTAL_ON_FAIL), wifiPortalOnFail);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::PACKET_REPEATS_PER_LOOP), packetRepeatsPerLoop);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::HOME_ASSISTANT_DISCOVERY_PREFIX), homeAssistantDiscoveryPrefix);
  this->setIfPresent(parsedSettings, FPSTR(SettingsKeys::DEFAULT_TRANSITION_PERIOD), defaultTransitionPeriod);

  if (parsedSettings.containsKey(FPSTR(SettingsKeys::WIFI_MODE))) {
    this->wifiMode = wifiModeFromString(parsedSettings[FPSTR(SettingsKeys::WIFI_MODE)]);
  }

  if (parsedSettings.containsKey(FPSTR(SettingsKeys::RF24_CHANNELS))) {
    JsonArray arr = parsedSettings[FPSTR(SettingsKeys::RF24_CHANNELS)];
    rf24Channels = JsonHelpers::jsonArrToVector<RF24Channel, String>(arr, RF24ChannelHelpers::valueFromName);
  }

  if (parsedSettings.containsKey(FPSTR(SettingsKeys::RF24_LISTEN_CHANNEL))) {
    this->rf24ListenChannel = RF24ChannelHelpers::valueFromName(parsedSettings[FPSTR(SettingsKeys::RF24_LISTEN_CHANNEL)]);
  }

  if (parsedSettings.containsKey(FPSTR(SettingsKeys::RF24_POWER_LEVEL))) {
    this->rf24PowerLevel = RF24PowerLevelHelpers::valueFromName(parsedSettings[FPSTR(SettingsKeys::RF24_POWER_LEVEL)]);
  }

  if (parsedSettings.containsKey(FPSTR(SettingsKeys::LED_MODE_WIFI_CONFIG))) {
    this->ledModeWifiConfig = LEDStatus::stringToLEDMode(parsedSettings[FPSTR(SettingsKeys::LED_MODE_WIFI_CONFIG)]);
  }

  if (parsedSettings.containsKey(FPSTR(SettingsKeys::LED_MODE_WIFI_FAILED))) {
    this->ledModeWifiFailed = LEDStatus::stringToLEDMode(parsedSettings[FPSTR(SettingsKeys::LED_MODE_WIFI_FAILED)]);
  }

  if (parsedSettings.containsKey(FPSTR(SettingsKeys::LED_MODE_OPERATING))) {
    this->ledModeOperating = LEDStatus::stringToLEDMode(parsedSettings[FPSTR(SettingsKeys::LED_MODE_OPERATING)]);
  }

  if (parsedSettings.containsKey(FPSTR(SettingsKeys::LED_MODE_PACKET))) {
    this->ledModePacket = LEDStatus::stringToLEDMode(parsedSettings[FPSTR(SettingsKeys::LED_MODE_PACKET)]);
  }

  if (parsedSettings.containsKey(FPSTR(SettingsKeys::RADIO_INTERFACE_TYPE))) {
    this->radioInterfaceType = Settings::typeFromString(parsedSettings[FPSTR(SettingsKeys::RADIO_INTERFACE_TYPE)]);
  }

  if (parsedSettings.containsKey(FPSTR(SettingsKeys::DEVICE_IDS))) {
    JsonArray arr = parsedSettings[FPSTR(SettingsKeys::DEVICE_IDS)];
    updateDeviceIds(arr);
  }
  if (parsedSettings.containsKey(FPSTR(SettingsKeys::GATEWAY_CONFIGS))) {
    JsonArray arr = parsedSettings[FPSTR(SettingsKeys::GATEWAY_CONFIGS)];
    updateGatewayConfigs(arr);
  }
  if (parsedSettings.containsKey(FPSTR(SettingsKeys::GROUP_STATE_FIELDS))) {
    JsonArray arr = parsedSettings[FPSTR(SettingsKeys::GROUP_STATE_FIELDS)];
    groupStateFields = JsonHelpers::jsonArrToVector<GroupStateField, const char*>(arr, GroupStateFieldHelpers::getFieldByName);
  }

  // this key will only be present in old settings files, but for backwards
  // compatability, parse it if it's present.
  if (parsedSettings.containsKey(FPSTR(SettingsKeys::GROUP_ID_ALIASES))) {
    parseGroupIdAliases(parsedSettings);
  }

  return String();
}

std::map<String, GroupAlias>::const_iterator Settings::findAlias(MiLightRemoteType deviceType, uint16_t deviceId, uint8_t groupId) {
  BulbId searchId{ deviceId, groupId, deviceType };

  for (auto it = groupIdAliases.begin(); it != groupIdAliases.end(); ++it) {
    if (searchId == it->second.bulbId) {
      return it;
    }
  }

  return groupIdAliases.end();
}

void Settings::parseGroupIdAliases(JsonObject json) {
  JsonObject aliases = json[FPSTR(SettingsKeys::GROUP_ID_ALIASES)].as<JsonObject>();

  // Save group IDs that were deleted so that they can be processed by discovery
  // if necessary
  for (auto it = groupIdAliases.begin(); it != groupIdAliases.end(); ++it) {
    deletedGroupIdAliases[it->second.bulbId.getCompactId()] = it->second.bulbId;
  }

  groupIdAliases.clear();
  size_t id = 1;

  for (JsonPair kv : aliases) {
    JsonArray bulbIdProps = kv.value();
    BulbId bulbId = {
      bulbIdProps[1].as<uint16_t>(),
      bulbIdProps[2].as<uint8_t>(),
      MiLightRemoteTypeHelpers::remoteTypeFromString(bulbIdProps[0].as<String>())
    };
    groupIdAliases[kv.key().c_str()] = GroupAlias(id++, kv.key().c_str(), bulbId);

    // If added this round, do not mark as deleted.
    deletedGroupIdAliases.erase(bulbId.getCompactId());
  }
}

void Settings::dumpGroupIdAliases(JsonObject json) {
  JsonObject aliases = json[FPSTR(SettingsKeys::GROUP_ID_ALIASES)].to<JsonObject>();

  for (auto & groupIdAlias : groupIdAliases) {
    JsonArray bulbProps = aliases[groupIdAlias.first].to<JsonArray>();
    BulbId bulbId = groupIdAlias.second.bulbId;
    bulbProps.add(MiLightRemoteTypeHelpers::remoteTypeToString(bulbId.deviceType));
    bulbProps.add(bulbId.deviceId);
    bulbProps.add(bulbId.groupId);
  }
}

bool Settings::loadAliases(Settings &settings) {
  if (ProjectFS.exists(ALIASES_FILE)) {
    File f = ProjectFS.open(ALIASES_FILE, "r");
    ReadBufferingStream bufferedReader{f, 64};
    GroupAlias::loadAliases(bufferedReader, settings.groupIdAliases);

    // find current max id
    size_t maxId = 0;
    for (auto & alias : settings.groupIdAliases) {
      maxId = max(maxId, alias.second.id);
    }
    settings.groupIdAliasNextId = maxId + 1;

    printf("loaded %d aliases\n", settings.groupIdAliases.size());

    return true;
  } else {
    return false;
  }
}

bool Settings::load(Settings& settings) {
  bool shouldInit = false;

  if (ProjectFS.exists(SETTINGS_FILE)) {
    // Clear in-memory settings
    settings = Settings();

    File f = ProjectFS.open(SETTINGS_FILE, "r");

    JsonDocument json;
    auto error = deserializeJson(json, f);
    f.close();

    if (! error) {
      JsonObject parsedSettings = json.as<JsonObject>();
      settings.patch(parsedSettings);
    } else {
      DebugSerial.print(F("Error parsing saved settings file: "));
      DebugSerial.println(error.c_str());
      DebugSerial.println(F("contents:"));

      f = ProjectFS.open(SETTINGS_FILE, "r");
      DebugSerial.println(f.readString());

      return false;
    }
  } else {
    shouldInit = true;
  }

  // If we loaded aliases from the settings file but not the aliases file,
  // port them over to the aliases file.
  const bool settingKeyAliasesEmpty = settings.groupIdAliases.empty();
  const bool aliasesFileEmpty = loadAliases(settings);

  if (!settingKeyAliasesEmpty && aliasesFileEmpty) {
    DebugSerial.println(F("Porting aliases from settings file to aliases file"));
    shouldInit = true;
  }

  if (shouldInit) {
    settings.save();
  }

  return true;
}

void Settings::save() {
  File f = ProjectFS.open("/config.json.tmp", "w");

  if (!f) {
    DebugSerial.println(F("Opening settings temp file failed"));
    return;
  } else {
    WriteBufferingStream writer{f, 64};
    serialize(writer);
    writer.flush();
    f.close();

    ProjectFS.remove(SETTINGS_FILE);
    ProjectFS.rename("/config.json.tmp", SETTINGS_FILE);
  }

  File aliasesFile = ProjectFS.open("/aliases.bin.tmp", "w");

  if (!aliasesFile) {
    DebugSerial.println(F("Opening aliases temp file failed"));
  } else {
    WriteBufferingStream aliases{aliasesFile, 64};
    GroupAlias::saveAliases(aliases, groupIdAliases);
    aliases.flush();
    aliasesFile.close();

    ProjectFS.remove(ALIASES_FILE);
    ProjectFS.rename("/aliases.bin.tmp", ALIASES_FILE);
  }
}

void Settings::serialize(Print& stream, const bool prettyPrint, const bool includePlaintextPasswords) const {
  JsonDocument root;

  root[FPSTR(SettingsKeys::ADMIN_USERNAME)] = this->adminUsername;
  root[FPSTR(SettingsKeys::ADMIN_PASSWORD)] = includePlaintextPasswords ? this->adminPassword : (this->adminPassword.length() > 0 ? String("***") : String(""));
  root[FPSTR(SettingsKeys::CE_PIN)] = this->cePin;
  root[FPSTR(SettingsKeys::CSN_PIN)] = this->csnPin;
  root[FPSTR(SettingsKeys::RESET_PIN)] = this->resetPin;
  root[FPSTR(SettingsKeys::LED_PIN)] = this->ledPin;
  root[FPSTR(SettingsKeys::RADIO_INTERFACE_TYPE)] = typeToString(this->radioInterfaceType);
  root[FPSTR(SettingsKeys::PACKET_REPEATS)] = this->packetRepeats;
  root[FPSTR(SettingsKeys::HTTP_REPEAT_FACTOR)] = this->httpRepeatFactor;
  root[FPSTR(SettingsKeys::AUTO_RESTART_PERIOD)] = this->_autoRestartPeriod;
  root[FPSTR(SettingsKeys::MQTT_SERVER)] = this->_mqttServer;
  root[FPSTR(SettingsKeys::MQTT_USERNAME)] = this->mqttUsername;
  root[FPSTR(SettingsKeys::MQTT_PASSWORD)] = includePlaintextPasswords ? this->mqttPassword : (this->mqttPassword.length() > 0 ? String("***") : String(""));
  root[FPSTR(SettingsKeys::MQTT_TOPIC_PATTERN)] = this->mqttTopicPattern;
  root[FPSTR(SettingsKeys::MQTT_UPDATE_TOPIC_PATTERN)] = this->mqttUpdateTopicPattern;
  root[FPSTR(SettingsKeys::MQTT_STATE_TOPIC_PATTERN)] = this->mqttStateTopicPattern;
  root[FPSTR(SettingsKeys::MQTT_CLIENT_STATUS_TOPIC)] = this->mqttClientStatusTopic;
  root[FPSTR(SettingsKeys::SIMPLE_MQTT_CLIENT_STATUS)] = this->simpleMqttClientStatus;
  root[FPSTR(SettingsKeys::DISCOVERY_PORT)] = this->discoveryPort;
  root[FPSTR(SettingsKeys::LISTEN_REPEATS)] = this->listenRepeats;
  root[FPSTR(SettingsKeys::STATE_FLUSH_INTERVAL)] = this->stateFlushInterval;
  root[FPSTR(SettingsKeys::MQTT_STATE_RATE_LIMIT)] = this->mqttStateRateLimit;
  root[FPSTR(SettingsKeys::MQTT_DEBOUNCE_DELAY)] = this->mqttDebounceDelay;
  root[FPSTR(SettingsKeys::MQTT_RETAIN)] = this->mqttRetain;
  root[FPSTR(SettingsKeys::PACKET_REPEAT_THROTTLE_SENSITIVITY)] = this->packetRepeatThrottleSensitivity;
  root[FPSTR(SettingsKeys::PACKET_REPEAT_THROTTLE_THRESHOLD)] = this->packetRepeatThrottleThreshold;
  root[FPSTR(SettingsKeys::PACKET_REPEAT_MINIMUM)] = this->packetRepeatMinimum;
  root[FPSTR(SettingsKeys::ENABLE_AUTOMATIC_MODE_SWITCHING)] = this->enableAutomaticModeSwitching;
  root[FPSTR(SettingsKeys::LED_MODE_WIFI_CONFIG)] = LEDStatus::LEDModeToString(this->ledModeWifiConfig);
  root[FPSTR(SettingsKeys::LED_MODE_WIFI_FAILED)] = LEDStatus::LEDModeToString(this->ledModeWifiFailed);
  root[FPSTR(SettingsKeys::LED_MODE_OPERATING)] = LEDStatus::LEDModeToString(this->ledModeOperating);
  root[FPSTR(SettingsKeys::LED_MODE_PACKET)] = LEDStatus::LEDModeToString(this->ledModePacket);
  root[FPSTR(SettingsKeys::LED_MODE_PACKET_COUNT)] = this->ledModePacketCount;
  root[FPSTR(SettingsKeys::HOSTNAME)] = this->hostname;
  root[FPSTR(SettingsKeys::RF24_POWER_LEVEL)] = RF24PowerLevelHelpers::nameFromValue(this->rf24PowerLevel);
  root[FPSTR(SettingsKeys::RF24_LISTEN_CHANNEL)] = RF24ChannelHelpers::nameFromValue(rf24ListenChannel);
  root[FPSTR(SettingsKeys::WIFI_STATIC_IP)] = this->wifiStaticIP;
  root[FPSTR(SettingsKeys::WIFI_STATIC_IP_GATEWAY)] = this->wifiStaticIPGateway;
  root[FPSTR(SettingsKeys::WIFI_STATIC_IP_NETMASK)] = this->wifiStaticIPNetmask;
  root[FPSTR(SettingsKeys::WIFI_SSID)] = this->wifiSsid;
  root[FPSTR(SettingsKeys::WIFI_PASSWORD)] = includePlaintextPasswords ? this->wifiPassword : (this->wifiPassword.length() > 0 ? String("***") : String(""));
  root[FPSTR(SettingsKeys::WIFI_SSID_SECONDARY)] = this->wifiSsidSecondary;
  root[FPSTR(SettingsKeys::WIFI_PASSWORD_SECONDARY)] = includePlaintextPasswords ? this->wifiPasswordSecondary : (this->wifiPasswordSecondary.length() > 0 ? String("***") : String(""));
  root[FPSTR(SettingsKeys::WIFI_DNS)] = this->wifiDns;
  root[FPSTR(SettingsKeys::WIFI_PORTAL_ON_FAIL)] = this->wifiPortalOnFail;
  root[FPSTR(SettingsKeys::PACKET_REPEATS_PER_LOOP)] = this->packetRepeatsPerLoop;
  root[FPSTR(SettingsKeys::HOME_ASSISTANT_DISCOVERY_PREFIX)] = this->homeAssistantDiscoveryPrefix;
  root[FPSTR(SettingsKeys::WIFI_MODE)] = wifiModeToString(this->wifiMode);
  root[FPSTR(SettingsKeys::DEFAULT_TRANSITION_PERIOD)] = this->defaultTransitionPeriod;

  JsonArray channelArr = root[FPSTR(SettingsKeys::RF24_CHANNELS)].to<JsonArray>();
  JsonHelpers::vectorToJsonArr<RF24Channel, String>(channelArr, rf24Channels, RF24ChannelHelpers::nameFromValue);

  JsonArray deviceIdsArr = root[FPSTR(SettingsKeys::DEVICE_IDS)].to<JsonArray>();
  JsonHelpers::copyFrom<uint16_t>(deviceIdsArr, this->deviceIds);

  JsonArray gatewayConfigsArr = root[FPSTR(SettingsKeys::GATEWAY_CONFIGS)].to<JsonArray>();
  for (size_t i = 0; i < this->gatewayConfigs.size(); i++) {
    JsonArray elmt = gatewayConfigsArr.add<JsonArray>();
    elmt.add(this->gatewayConfigs[i]->deviceId);
    elmt.add(this->gatewayConfigs[i]->port);
    elmt.add(this->gatewayConfigs[i]->protocolVersion);
  }

  JsonArray groupStateFieldArr = root[FPSTR(SettingsKeys::GROUP_STATE_FIELDS)].to<JsonArray>();
  JsonHelpers::vectorToJsonArr<GroupStateField, const char*>(groupStateFieldArr, groupStateFields, GroupStateFieldHelpers::getFieldName);

  if (prettyPrint) {
    serializeJsonPretty(root, stream);
  } else {
    serializeJson(root, stream);
  }
}

String Settings::mqttServer() {
  int pos = PORT_POSITION(_mqttServer);

  if (pos == -1) {
    return _mqttServer;
  } else {
    return _mqttServer.substring(0, pos);
  }
}

uint16_t Settings::mqttPort() {
  int pos = PORT_POSITION(_mqttServer);

  if (pos == -1) {
    return DEFAULT_MQTT_PORT;
  } else {
    return atoi(_mqttServer.c_str() + pos + 1);
  }
}

RadioInterfaceType Settings::typeFromString(const String& s) {
  if (s.equalsIgnoreCase("lt8900")) {
    return LT8900;
  } else {
    return nRF24;
  }
}

String Settings::typeToString(RadioInterfaceType type) {
  switch (type) {
    case LT8900:
      return "LT8900";

    case nRF24:
    default:
      return "nRF24";
  }
}

WifiMode Settings::wifiModeFromString(const String& mode) {
  if (mode.equalsIgnoreCase("b")) {
    return WifiMode::B;
  } else if (mode.equalsIgnoreCase("g")) {
    return WifiMode::G;
  } else {
    return WifiMode::N;
  }
}

String Settings::wifiModeToString(WifiMode mode) {
  switch (mode) {
    case WifiMode::B:
      return "b";
    case WifiMode::G:
      return "g";
    case WifiMode::N:
    default:
      return "n";
  }
}

void Settings::addAlias(const char *alias, const BulbId &bulbId) {
  groupIdAliases[alias] = GroupAlias(groupIdAliasNextId++, alias, bulbId);
}

bool Settings::deleteAlias(size_t id) {
  for (auto it = groupIdAliases.begin(); it != groupIdAliases.end(); ++it) {
    if (it->second.id == id) {
      BulbId bulbId = it->second.bulbId;
      groupIdAliases.erase(it);
      deletedGroupIdAliases[bulbId.getCompactId()] = bulbId;

      return true;
    }
  }

  return false;
}

std::map<String, GroupAlias>::const_iterator Settings::findAliasById(size_t id) {
  for (auto it = groupIdAliases.begin(); it != groupIdAliases.end(); ++it) {
    if (it->second.id == id) {
      return it;
    }
  }

  return groupIdAliases.end();
}