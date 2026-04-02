#include <BulbStateUpdater.h>

BulbStateUpdater::BulbStateUpdater(Settings& settings, MqttClient& mqttClient, GroupStateStore& stateStore)
  : settings(settings),
    mqttClient(mqttClient),
    stateStore(stateStore),
    lastFlush(0),
    lastQueue(0),
    enabled(true)
{ }

void BulbStateUpdater::enable() {
  this->enabled = true;
}

void BulbStateUpdater::disable() {
  this->enabled = false;
}

void BulbStateUpdater::enqueueUpdate(BulbId bulbId, GroupState& groupState) {
  staleGroups.push(bulbId);
  //Remember time, when queue was added for debounce delay
  lastQueue = millis();

}

void BulbStateUpdater::loop() {
  while (canFlush() && staleGroups.size() > 0) {
    BulbId bulbId = staleGroups.shift();
    GroupState* groupState = stateStore.get(bulbId);

    if (groupState != NULL && groupState->isMqttDirty()) {
      flushGroup(bulbId, *groupState);
      groupState->clearMqttDirty();
    }
  }
}

inline void BulbStateUpdater::flushGroup(BulbId bulbId, GroupState& state) {
  StaticJsonDocument<MILIGHT_MQTT_JSON_BUFFER_SIZE> json;
  JsonObject message = json.to<JsonObject>();
  state.applyState(message, bulbId, settings.groupStateFields);

  if (json.overflowed()) {
    Serial.println(F("ERROR: State is too large for MQTT buffer, continuing anyway. Consider increasing MILIGHT_MQTT_JSON_BUFFER_SIZE."));
  }

  size_t documentSize = measureJson(message);
  char buffer[documentSize + 1];
  serializeJson(json, buffer, sizeof(buffer));

  const MiLightRemoteConfig* config = MiLightRemoteConfig::fromType(bulbId.deviceType);
  if (config == NULL) {
    Serial.println(F("BulbStateUpdater: unknown device type, skipping flush"));
    return;
  }

  mqttClient.sendState(
    *config,
    bulbId.deviceId,
    bulbId.groupId,
    buffer
  );

  lastFlush = millis();
}

inline bool BulbStateUpdater::canFlush() const {
  unsigned long now = millis();
  return enabled && ((now - lastFlush) >= settings.mqttStateRateLimit) && ((now - lastQueue) >= settings.mqttDebounceDelay);
}
