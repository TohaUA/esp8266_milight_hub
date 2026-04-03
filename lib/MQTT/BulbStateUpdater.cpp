#include <BulbStateUpdater.h>
#include <DebugSerial.h>

BulbStateUpdater::BulbStateUpdater(Settings& settings, MqttClient& mqttClient, GroupStateStore& stateStore)
  : settings(settings),
    mqttClient(mqttClient),
    stateStore(stateStore),
    lastFlush(0),
    lastQueue(0),
    enabled(true) {}

void BulbStateUpdater::enable() {
  this->enabled = true;
}

void BulbStateUpdater::disable() {
  this->enabled = false;
}

void BulbStateUpdater::enqueueUpdate(BulbId bulbId, GroupState& groupState) {
  // Deduplicate: skip if already queued
  for (size_t i = 0; i < staleGroups.size(); i++) {
    if (staleGroups[i] == bulbId) {
      lastQueue = millis();
      return;
    }
  }
  if (staleGroups.isFull()) {
    DebugSerial.println(F("WARN: MQTT state update queue full, oldest entry dropped"));
  }
  staleGroups.push(bulbId);
  lastQueue = millis();
}

void BulbStateUpdater::loop() {
  while (canFlush() && staleGroups.size() > 0 && mqttClient.isConnected()) {
    BulbId bulbId = staleGroups.shift();
    GroupState* groupState = stateStore.get(bulbId);

    if (groupState != NULL && groupState->isMqttDirty()) {
      flushGroup(bulbId, *groupState);
      groupState->clearMqttDirty();
    }
  }
}

inline void BulbStateUpdater::flushGroup(BulbId bulbId, GroupState& state) {
  JsonDocument json;
  JsonObject message = json.to<JsonObject>();
  state.applyState(message, bulbId, settings.groupStateFields);

  if (json.overflowed()) {
    DebugSerial.println(F(
      "ERROR: State is too large for MQTT buffer, continuing anyway. Consider increasing MILIGHT_MQTT_JSON_BUFFER_SIZE."
    ));
  }

  char buffer[MILIGHT_MQTT_JSON_BUFFER_SIZE];
  serializeJson(json, buffer, sizeof(buffer));

  const MiLightRemoteConfig* config = MiLightRemoteConfig::fromType(bulbId.deviceType);
  if (config == NULL) {
    DebugSerial.println(F("BulbStateUpdater: unknown device type, skipping flush"));
    return;
  }

  mqttClient.sendState(*config, bulbId.deviceId, bulbId.groupId, buffer);

  lastFlush = millis();
}

void BulbStateUpdater::syncAll() {
  if (!mqttClient.isConnected()) {
    return;
  }

  ListNode<GroupCacheNode*>* cur = stateStore.getCacheHead();
  while (cur != NULL) {
    flushGroup(cur->data->id, cur->data->state);
    cur = cur->next;
    yield();
  }
}

inline bool BulbStateUpdater::canFlush() const {
  unsigned long now = millis();
  return enabled && ((now - lastFlush) >= settings.mqttStateRateLimit) &&
         ((now - lastQueue) >= settings.mqttDebounceDelay);
}
