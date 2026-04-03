// #if defined(ARDUINO) && defined(UNIT_TEST)

#include <ProjectFS.h>
#include <Arduino.h>

#include <GroupState.h>
#include <GroupStateStore.h>
#include <GroupStateCache.h>
#include <GroupStatePersistence.h>

#include <RgbCctPacketFormatter.h>
#include <FUT091PacketFormatter.h>
#include <Units.h>

#include <Settings.h>
#include "unity.h"

#ifdef ESP32
  #include <SPIFFS.h>
#endif

//================================================================================
// Packet formatter
//================================================================================

template <typename T>
void run_packet_test(uint8_t* packet, PacketFormatter* packetFormatter, const BulbId& expectedBulbId, const String& expectedKey, const T expectedValue) {
  GroupStateStore stateStore(10, 0);
  Settings settings;
  RgbCctPacketFormatter formatter;
  DynamicJsonDocument doc(1024);  // Remplace DynamicJsonBuffer par DynamicJsonDocument
  JsonObject result = doc.to<JsonObject>();  // Crée un JsonObject à partir de doc

  packetFormatter->prepare(0, 0);
  BulbId bulbId = packetFormatter->parsePacket(packet, result);

  TEST_ASSERT_EQUAL_INT_MESSAGE(expectedBulbId.deviceId, bulbId.deviceId, "Should get the expected device ID");
  TEST_ASSERT_EQUAL_INT_MESSAGE(expectedBulbId.groupId, bulbId.groupId, "Should get the expected group ID");
  TEST_ASSERT_EQUAL_INT_MESSAGE(expectedBulbId.deviceType, bulbId.deviceType, "Should get the expected remote type");

  TEST_ASSERT_TRUE_MESSAGE(result.containsKey(expectedKey), "Parsed packet should be for expected command type");
  TEST_ASSERT_TRUE_MESSAGE(result[expectedKey] == expectedValue, "Parsed packet should have expected value");
}

void test_fut092_packet_formatter() {
  RgbCctPacketFormatter packetFormatter;

  uint8_t onPacket[] = {0x00, 0xDB, 0xE1, 0x24, 0x66, 0xCA, 0x54, 0x66, 0xD2};
  run_packet_test(
    onPacket,
    &packetFormatter,
    BulbId(1, 1, REMOTE_TYPE_RGB_CCT),
    "state",
    "OFF"
  );

  uint8_t minColorTempPacket[] = {0x00, 0xDB, 0xE1, 0x24, 0x64, 0x3C, 0x47, 0x66, 0x31};
  run_packet_test(
    minColorTempPacket,
    &packetFormatter,
    BulbId(1, 1, REMOTE_TYPE_RGB_CCT),
    "color_temp",
    COLOR_TEMP_MIN_MIREDS
  );

  uint8_t maxColorTempPacket[] = {0x00, 0xDB, 0xE1, 0x24, 0x64, 0x94, 0x62, 0x66, 0x88};
  run_packet_test(
    maxColorTempPacket,
    &packetFormatter,
    BulbId(1, 1, REMOTE_TYPE_RGB_CCT),
    "color_temp",
    COLOR_TEMP_MAX_MIREDS
  );
}

void test_fut091_packet_formatter() {
  FUT091PacketFormatter packetFormatter;

  uint8_t onPacket[] = {0x00, 0xDC, 0xE1, 0x24, 0x66, 0xCA, 0xBA, 0x66, 0xB5};
  run_packet_test(
    onPacket,
    &packetFormatter,
    BulbId(1, 1, REMOTE_TYPE_FUT091),
    "state",
    "OFF"
  );

  uint8_t minColorTempPacket[] = {0x00, 0xDC, 0xE1, 0x24, 0x64, 0x8D, 0xB9, 0x66, 0x71};
  run_packet_test(
    minColorTempPacket,
    &packetFormatter,
    BulbId(1, 1, REMOTE_TYPE_FUT091),
    "color_temp",
    COLOR_TEMP_MIN_MIREDS
  );

  uint8_t maxColorTempPacket[] = {0x00, 0xDC, 0xE1, 0x24, 0x64, 0x55, 0xB7, 0x66, 0x27};
  run_packet_test(
    maxColorTempPacket,
    &packetFormatter,
    BulbId(1, 1, REMOTE_TYPE_FUT091),
    "color_temp",
    COLOR_TEMP_MAX_MIREDS
  );
}

//================================================================================
// Group State
//================================================================================

GroupState color() {
  GroupState s;

  s.setState(MiLightStatus::ON);
  s.setBulbMode(BulbMode::BULB_MODE_COLOR);
  s.setHue(1);
  s.setSaturation(10);
  s.setBrightness(100);

  return s;
}

void test_init_state() {
  GroupState s;

  TEST_ASSERT_EQUAL(s.getBulbMode(), BulbMode::BULB_MODE_WHITE);
  TEST_ASSERT_EQUAL(s.isSetBrightness(), false);
}

void test_state_updates() {
  GroupState s = color();

  // Make sure values are packed and unpacked correctly
  TEST_ASSERT_EQUAL(s.getBulbMode(), BulbMode::BULB_MODE_COLOR);
  TEST_ASSERT_EQUAL(s.getBrightness(), 100);
  TEST_ASSERT_EQUAL(s.getHue(), 1);
  TEST_ASSERT_EQUAL(s.getSaturation(), 10);

  // Make sure brightnesses are tied to mode
  s.setBulbMode(BulbMode::BULB_MODE_WHITE);
  s.setBrightness(0);

  TEST_ASSERT_EQUAL(s.getBulbMode(), BulbMode::BULB_MODE_WHITE);
  TEST_ASSERT_EQUAL(s.getBrightness(), 0);

  s.setBulbMode(BulbMode::BULB_MODE_COLOR);

  TEST_ASSERT_EQUAL(s.getBrightness(), 100);
}

void test_cache() {
  BulbId id1(1, 1, REMOTE_TYPE_FUT089);
  BulbId id2(1, 2, REMOTE_TYPE_FUT089);

  GroupState s = color();
  s.clearDirty();
  s.clearMqttDirty();

  GroupStateCache cache(1);
  GroupState* storedState = cache.get(id2);

  TEST_ASSERT_NULL_MESSAGE(storedState, "Should not retrieve value which hasn't been stored");

  cache.set(id1, s);
  storedState = cache.get(id1);

  TEST_ASSERT_NOT_NULL_MESSAGE(storedState, "Should retrieve a value");
  TEST_ASSERT_TRUE_MESSAGE(s == *storedState, "State should be the same when retrieved");

  cache.set(id2, s);
  storedState = cache.get(id2);

  TEST_ASSERT_NOT_NULL_MESSAGE(storedState, "Should retrieve a value");
  TEST_ASSERT_TRUE_MESSAGE(s == *storedState, "State should be the same when retrieved");

  storedState = cache.get(id1);

  TEST_ASSERT_NULL_MESSAGE(storedState, "Should evict old entry from cache");
}

void test_persistence() {
  BulbId id1(1, 1, REMOTE_TYPE_FUT089);
  BulbId id2(1, 2, REMOTE_TYPE_FUT089);

  GroupStatePersistence persistence;

  persistence.clear(id1);
  persistence.clear(id2);

  GroupState storedState;
  GroupState s = color();
  s.clearDirty();
  s.clearMqttDirty();

  GroupState defaultState = GroupState::defaultState(REMOTE_TYPE_FUT089);

  persistence.get(id1, storedState);
  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(defaultState), "Should start with clean state");
  persistence.get(id2, storedState);
  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(defaultState), "Should start with clean state");

  persistence.set(id1, s);

  persistence.get(id2, storedState);
  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(defaultState), "Should return default for state that hasn't been stored");

  persistence.get(id1, storedState);
  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(s), "Should retrieve state from flash without modification");

  GroupState newState = s;
  newState.setBulbMode(BulbMode::BULB_MODE_WHITE);
  newState.setBrightness(255);
  persistence.set(id2, newState);

  persistence.get(id1, storedState);

  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(s), "Should retrieve unmodified state");

  persistence.get(id2, storedState);

  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(newState), "Should retrieve modified state");
}

void test_store() {
  BulbId id1(1, 1, REMOTE_TYPE_FUT089);
  BulbId id2(1, 2, REMOTE_TYPE_FUT089);

  GroupStateStore store(4, 0);
  GroupStatePersistence persistence;

  persistence.clear(id1);
  persistence.clear(id2);

  GroupState initState = color();
  GroupState initState2 = color();
  GroupState defaultState = GroupState::defaultState(REMOTE_TYPE_FUT089);
  initState2.setBrightness(50);

  GroupState* storedState;

  storedState = store.get(id2);
  TEST_ASSERT_TRUE_MESSAGE(*storedState == defaultState, "Should return default for state that hasn't been stored");

  store.set(id1, initState);
  storedState = store.get(id1);

  TEST_ASSERT_TRUE_MESSAGE(storedState->isEqualIgnoreDirty(initState), "Should return stored state.  Will not be cached because of internal group 0 lookups");

  store.flush();
  storedState = store.get(id1);
  TEST_ASSERT_FALSE_MESSAGE(storedState->isDirty(), "Should not be dirty after flushing");
  TEST_ASSERT_TRUE_MESSAGE(storedState->isEqualIgnoreDirty(initState), "Should return cached state after flushing");

  store.set(id2, defaultState);
  storedState = store.get(id2);
  TEST_ASSERT_TRUE_MESSAGE(storedState->isEqualIgnoreDirty(defaultState), "Should return cached state");

  storedState = store.get(id1);
  TEST_ASSERT_TRUE_MESSAGE(storedState->isEqualIgnoreDirty(initState), "Should return persisted state");
}

void test_group_0() {
  BulbId group0Id(1, 0, REMOTE_TYPE_FUT089);
  BulbId id1(1, 1, REMOTE_TYPE_FUT089);
  BulbId id2(1, 2, REMOTE_TYPE_FUT089);

  // cache 1 item, flush immediately
  GroupStateStore store(10, 0);
  GroupStatePersistence persistence;

  persistence.clear(id1);
  persistence.clear(id2);

  GroupState initState = color();
  GroupState initState2 = color();
  GroupState storedState;
  GroupState expectedState;
  GroupState group0State;

  initState2.setBrightness(255);
  group0State.setHue(100);

  store.set(id1, initState);
  store.set(id2, initState2);

  TEST_ASSERT_FALSE_MESSAGE(group0State.isEqualIgnoreDirty(initState), "group0 state should be different than initState");
  TEST_ASSERT_FALSE_MESSAGE(group0State.isEqualIgnoreDirty(initState2), "group0 state should be different than initState2");

  storedState = *store.get(id1);
  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(initState), "Should fetch persisted state");

  storedState = *store.get(id2);
  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(initState2), "Should fetch persisted state");

  store.set(group0Id, group0State);

  storedState = *store.get(id1);
  expectedState = initState;
  expectedState.setHue(group0State.getHue());

  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(expectedState), "Saving group 0 should only update changed field");

  storedState = *store.get(id2);
  expectedState = initState2;
  expectedState.setHue(group0State.getHue());
  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(expectedState), "Saving group 0 should only update changed field");

  // Test that state for group 0 is persisted
  storedState = *store.get(group0Id);
  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(group0State), "Group 0 state should not be stored -- should return default state");

  // Test that states for constituent groups are properly updated
  initState.setHue(0);
  initState2.setHue(100);
  initState.setBrightness(50);
  initState2.setBrightness(70);
  store.set(id1, initState);
  store.set(id2, initState2);

  storedState = *store.get(group0Id);
  storedState.setHue(200);
  TEST_ASSERT_FALSE_MESSAGE(storedState.isSetBrightness(), "Should not have a set field for group 0 brightness");

  store.set(group0Id, storedState);

  storedState = *store.get(id1);
  TEST_ASSERT_TRUE_MESSAGE(storedState.getBrightness() == 50, "UNSET field in group 0 update SHOULD NOT overwrite constituent group field");
  TEST_ASSERT_TRUE_MESSAGE(storedState.getHue() == 200, "SET field in group 0 update SHOULD overwrite constituent group field");

  storedState = *store.get(id2);
  TEST_ASSERT_TRUE_MESSAGE(storedState.getBrightness() == 70, "UNSET field in group 0 update SHOULD NOT overwrite constituent group field");
  TEST_ASSERT_TRUE_MESSAGE(storedState.getHue() == 200, "SET field in group 0 update SHOULD overwrite constituent group field");

  // Should persist group 0 for device types with 0 groups
  BulbId rgbId(1, 0, REMOTE_TYPE_RGB);
  GroupState rgbState = GroupState::defaultState(REMOTE_TYPE_RGB);
  rgbState.setHue(100);
  rgbState.setBrightness(100);

  store.set(rgbId, rgbState);
  store.flush();

  storedState = *store.get(rgbId);

  TEST_ASSERT_TRUE_MESSAGE(storedState.isEqualIgnoreDirty(rgbState), "Should persist group 0 for device type with no groups");
}

//================================================================================
// Settings Validation
//================================================================================

String patchWith(const char* json) {
  Settings s;
  JsonDocument doc;
  deserializeJson(doc, json);
  return s.patch(doc.as<JsonObject>());
}

void test_valid_settings_accepted() {
  String err = patchWith("{\"packet_repeats\":50,\"hostname\":\"my-hub\",\"radio_interface_type\":\"nRF24\"}");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", err.c_str(), "Valid settings should be accepted");
}

void test_reject_invalid_enum() {
  String err = patchWith("{\"radio_interface_type\":\"INVALID\"}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject invalid radio_interface_type");
  TEST_ASSERT_TRUE_MESSAGE(err.indexOf("radio_interface_type") >= 0, "Error should name the field");
}

void test_reject_out_of_range_int() {
  String err = patchWith("{\"packet_repeats\":0}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject packet_repeats=0");

  err = patchWith("{\"packet_repeats\":1001}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject packet_repeats=1001");

  err = patchWith("{\"packet_repeats\":50}");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", err.c_str(), "Should accept packet_repeats=50");
}

void test_reject_long_string() {
  char json[200];
  char longHost[65];
  memset(longHost, 'a', 64);
  longHost[64] = 0;
  snprintf(json, sizeof(json), "{\"hostname\":\"%s\"}", longHost);

  String err = patchWith(json);
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject hostname > 63 chars");
}

void test_reject_empty_hostname() {
  String err = patchWith("{\"hostname\":\"\"}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject empty hostname");
}

void test_reject_invalid_led_mode() {
  String err = patchWith("{\"led_mode_operating\":\"Bogus\"}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject invalid LED mode");

  err = patchWith("{\"led_mode_operating\":\"Slow blip\"}");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", err.c_str(), "Should accept valid LED mode");
}

void test_reject_invalid_rf24_channel_array() {
  String err = patchWith("{\"rf24_channels\":[]}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject empty rf24_channels");

  err = patchWith("{\"rf24_channels\":[\"LOW\",\"BOGUS\"]}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject invalid channel name");

  err = patchWith("{\"rf24_channels\":[\"LOW\",\"MID\",\"HIGH\"]}");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", err.c_str(), "Should accept valid channels");
}

void test_reject_invalid_gateway_config() {
  String err = patchWith("{\"gateway_configs\":[[1234,0,6]]}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject port=0");

  err = patchWith("{\"gateway_configs\":[[1234,5555,7]]}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject protocol=7");

  err = patchWith("{\"gateway_configs\":[[1234,5555,6]]}");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", err.c_str(), "Should accept valid gateway config");
}

void test_reject_invalid_pin() {
  String err = patchWith("{\"ce_pin\":40}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject ce_pin=40");

  err = patchWith("{\"led_pin\":-40}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject led_pin=-40");
}

void test_state_flush_interval_validation() {
  String err = patchWith("{\"state_flush_interval\":50}");
  TEST_ASSERT_TRUE_MESSAGE(err.length() > 0, "Should reject state_flush_interval=50 (below 100, non-zero)");

  err = patchWith("{\"state_flush_interval\":0}");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", err.c_str(), "Should accept 0 (disabled)");

  err = patchWith("{\"state_flush_interval\":10000}");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", err.c_str(), "Should accept 10000");
}

// setup connects serial, runs test cases (upcoming)
void setup() {
  delay(2000);
  ProjectFS.begin();
  Serial.begin(9600);

  UNITY_BEGIN();

  RUN_TEST(test_init_state);
  RUN_TEST(test_state_updates);
  RUN_TEST(test_cache);
  RUN_TEST(test_persistence);
  RUN_TEST(test_store);
  RUN_TEST(test_group_0);

  RUN_TEST(test_fut091_packet_formatter);
  RUN_TEST(test_fut092_packet_formatter);

  RUN_TEST(test_valid_settings_accepted);
  RUN_TEST(test_reject_invalid_enum);
  RUN_TEST(test_reject_out_of_range_int);
  RUN_TEST(test_reject_long_string);
  RUN_TEST(test_reject_empty_hostname);
  RUN_TEST(test_reject_invalid_led_mode);
  RUN_TEST(test_reject_invalid_rf24_channel_array);
  RUN_TEST(test_reject_invalid_gateway_config);
  RUN_TEST(test_reject_invalid_pin);
  RUN_TEST(test_state_flush_interval_validation);

  UNITY_END();
}

void loop() {
  // nothing to be done here.
}

// #endif