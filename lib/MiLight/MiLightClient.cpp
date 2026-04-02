#include <MiLightClient.h>
#include <MiLightRadioConfig.h>
#include <Arduino.h>
#include <Units.h>
#include <TokenIterator.h>
#include <ParsedColor.h>
#include <MiLightCommands.h>
#include <DebugSerial.h>

static const uint8_t STATUS_UNDEFINED = 255;

const char* MiLightClient::FIELD_ORDERINGS[] = {
  // These are handled manually
  // GroupStateFieldNames::STATE,
  // GroupStateFieldNames::STATUS,
  GroupStateFieldNames::HUE,
  GroupStateFieldNames::SATURATION,
  GroupStateFieldNames::KELVIN,
  GroupStateFieldNames::TEMPERATURE,
  GroupStateFieldNames::COLOR_TEMP,
  GroupStateFieldNames::MODE,
  GroupStateFieldNames::EFFECT,
  GroupStateFieldNames::COLOR,
  // Level/Brightness must be processed last because they're specific to a particular bulb mode.
  // So make sure bulb mode is set before applying level/brightness.
  GroupStateFieldNames::LEVEL,
  GroupStateFieldNames::BRIGHTNESS,
  GroupStateFieldNames::COMMAND,
  GroupStateFieldNames::COMMANDS
};

static void setStatus(MiLightClient* client, JsonVariant val) {
  client->updateStatus(parseMilightStatus(val));
}

static void setBrightness255(MiLightClient* client, JsonVariant val) {
  client->updateBrightness(Units::rescale<uint16_t, uint16_t>(val.as<uint16_t>(), 100, 255));
}

static void setColorTemp(MiLightClient* client, JsonVariant val) {
  client->updateTemperature(Units::miredsToWhiteVal(val.as<uint16_t>(), 100));
}

const MiLightClient::FieldSetter MiLightClient::FIELD_SETTERS[] = {
  {GroupStateFieldNames::STATUS, setStatus},
  {GroupStateFieldNames::LEVEL, [](MiLightClient* c, JsonVariant v) { c->updateBrightness(v); }},
  {GroupStateFieldNames::BRIGHTNESS, setBrightness255},
  {GroupStateFieldNames::HUE, [](MiLightClient* c, JsonVariant v) { c->updateHue(v); }},
  {GroupStateFieldNames::SATURATION, [](MiLightClient* c, JsonVariant v) { c->updateSaturation(v); }},
  {GroupStateFieldNames::KELVIN, [](MiLightClient* c, JsonVariant v) { c->updateTemperature(v); }},
  {GroupStateFieldNames::TEMPERATURE, [](MiLightClient* c, JsonVariant v) { c->updateTemperature(v); }},
  {GroupStateFieldNames::COLOR_TEMP, setColorTemp},
  {GroupStateFieldNames::MODE, [](MiLightClient* c, JsonVariant v) { c->updateMode(v); }},
  {GroupStateFieldNames::COLOR, [](MiLightClient* c, JsonVariant v) { c->updateColor(v); }},
  {GroupStateFieldNames::EFFECT, [](MiLightClient* c, JsonVariant v) { c->handleEffect(v); }},
  {GroupStateFieldNames::COMMAND, [](MiLightClient* c, JsonVariant v) { c->handleCommand(v); }},
  {GroupStateFieldNames::COMMANDS, [](MiLightClient* c, JsonVariant v) { c->handleCommands(v); }}
};

const size_t MiLightClient::NUM_FIELD_SETTERS = sizeof(FIELD_SETTERS) / sizeof(FIELD_SETTERS[0]);

MiLightClient::MiLightClient(
  RadioSwitchboard& radioSwitchboard,
  PacketSender& packetSender,
  GroupStateStore* stateStore,
  Settings& settings,
  TransitionController& transitions
) : radioSwitchboard(radioSwitchboard)
  , updateBeginHandler(NULL)
  , updateEndHandler(NULL)
  , stateStore(stateStore)
  , settings(settings)
  , packetSender(packetSender)
  , transitions(transitions)
  , repeatsOverride(0)
{ }

void MiLightClient::setHeld(bool held) {
  currentRemote->packetFormatter->setHeld(held);
}

void MiLightClient::prepare(
  const MiLightRemoteConfig* config,
  const uint16_t deviceId,
  const uint8_t groupId
) {
  this->currentRemote = config;

  if (deviceId >= 0 && groupId >= 0) {
    currentRemote->packetFormatter->prepare(deviceId, groupId);
  }

  this->currentState = stateStore->get(deviceId, groupId, config->type);
}

void MiLightClient::prepare(
  const MiLightRemoteType type,
  const uint16_t deviceId,
  const uint8_t groupId
) {
  const MiLightRemoteConfig* config = MiLightRemoteConfig::fromType(type);
  if (config == NULL) {
    DebugSerial.println(F("MiLightClient::prepare - unknown remote type, ignoring"));
    return;
  }
  prepare(config, deviceId, groupId);
}

void MiLightClient::updateColorRaw(const uint8_t color) {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.printf("MiLightClient::updateColorRaw: Change color to %d\n", color);
#endif
  currentRemote->packetFormatter->updateColorRaw(color);
  flushPacket();
}

void MiLightClient::updateHue(const uint16_t hue) {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.printf("MiLightClient::updateHue: Change hue to %d\n", hue);
#endif
  currentRemote->packetFormatter->updateHue(hue);
  flushPacket();
}

void MiLightClient::updateBrightness(const uint8_t brightness) {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.printf("MiLightClient::updateBrightness: Change brightness to %d\n", brightness);
#endif
  currentRemote->packetFormatter->updateBrightness(brightness);
  flushPacket();
}

void MiLightClient::updateMode(uint8_t mode) {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.printf("MiLightClient::updateMode: Change mode to %d\n", mode);
#endif
  currentRemote->packetFormatter->updateMode(mode);
  flushPacket();
}

void MiLightClient::nextMode() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::nextMode: Switch to next mode"));
#endif
  currentRemote->packetFormatter->nextMode();
  flushPacket();
}

void MiLightClient::previousMode() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::previousMode: Switch to previous mode"));
#endif
  currentRemote->packetFormatter->previousMode();
  flushPacket();
}

void MiLightClient::modeSpeedDown() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::modeSpeedDown: Speed down\n"));
#endif
  currentRemote->packetFormatter->modeSpeedDown();
  flushPacket();
}
void MiLightClient::modeSpeedUp() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::modeSpeedUp: Speed up"));
#endif
  currentRemote->packetFormatter->modeSpeedUp();
  flushPacket();
}

void MiLightClient::updateStatus(MiLightStatus status, uint8_t groupId) {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.printf("MiLightClient::updateStatus: Status %s, groupId %d\n", status == MiLightStatus::OFF ? "OFF" : "ON", groupId);
#endif
  currentRemote->packetFormatter->updateStatus(status, groupId);
  flushPacket();
}

void MiLightClient::updateStatus(MiLightStatus status) {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.printf("MiLightClient::updateStatus: Status %s\n", status == MiLightStatus::OFF ? "OFF" : "ON");
#endif
  currentRemote->packetFormatter->updateStatus(status);
  flushPacket();
}

void MiLightClient::updateSaturation(const uint8_t value) {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.printf("MiLightClient::updateSaturation: Saturation %d\n", value);
#endif
  currentRemote->packetFormatter->updateSaturation(value);
  flushPacket();
}

void MiLightClient::updateColorWhite() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::updateColorWhite: Color white"));
#endif
  currentRemote->packetFormatter->updateColorWhite();
  flushPacket();
}

void MiLightClient::enableNightMode() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::enableNightMode: Night mode"));
#endif
  currentRemote->packetFormatter->enableNightMode();
  flushPacket();
}

void MiLightClient::pair() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::pair: Pair"));
#endif
  currentRemote->packetFormatter->pair();
  flushPacket();
}

void MiLightClient::unpair() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::unpair: Unpair"));
#endif
  currentRemote->packetFormatter->unpair();
  flushPacket();
}

void MiLightClient::increaseBrightness() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::increaseBrightness: Increase brightness"));
#endif
  currentRemote->packetFormatter->increaseBrightness();
  flushPacket();
}

void MiLightClient::decreaseBrightness() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::decreaseBrightness: Decrease brightness"));
#endif
  currentRemote->packetFormatter->decreaseBrightness();
  flushPacket();
}

void MiLightClient::increaseTemperature() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::increaseTemperature: Increase temperature"));
#endif
  currentRemote->packetFormatter->increaseTemperature();
  flushPacket();
}

void MiLightClient::decreaseTemperature() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.println(F("MiLightClient::decreaseTemperature: Decrease temperature"));
#endif
  currentRemote->packetFormatter->decreaseTemperature();
  flushPacket();
}

void MiLightClient::updateTemperature(const uint8_t temperature) {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.printf("MiLightClient::updateTemperature: Set temperature to %d\n", temperature);
#endif
  currentRemote->packetFormatter->updateTemperature(temperature);
  flushPacket();
}

void MiLightClient::command(uint8_t command, uint8_t arg) {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.printf("MiLightClient::command: Execute command %d, argument %d\n", command, arg);
#endif
  currentRemote->packetFormatter->command(command, arg);
  flushPacket();
}

void MiLightClient::toggleStatus() {
#ifdef DEBUG_CLIENT_COMMANDS
  DebugSerial.printf("MiLightClient::toggleStatus");
#endif
  currentRemote->packetFormatter->toggleStatus();
  flushPacket();
}

void MiLightClient::updateColor(JsonVariant json) {
  ParsedColor color = ParsedColor::fromJson(json);

  if (!color.success) {
    DebugSerial.println(F("Error parsing color field, unrecognized format"));
    return;
  }

  // We consider an RGB color "white" if all color intensities are roughly the
  // same value.  An unscientific value of 10 (~4%) is chosen.
  if ( abs(color.r - color.g) < RGB_WHITE_THRESHOLD
    && abs(color.g - color.b) < RGB_WHITE_THRESHOLD
    && abs(color.r - color.b) < RGB_WHITE_THRESHOLD) {
      this->updateColorWhite();
  } else {
    this->updateHue(color.hue);
    this->updateSaturation(color.saturation);
  }
}

void MiLightClient::update(JsonObject request) {
  if (this->updateBeginHandler) {
    this->updateBeginHandler();
  }

  const JsonVariant status = this->extractStatus(request);
  const uint8_t parsedStatus = this->parseStatus(status);
  const JsonVariant jsonTransition = request[RequestKeys::TRANSITION];
  float transition = 0;

  if (!jsonTransition.isNull()) {
    if (jsonTransition.is<float>()) {
      transition = jsonTransition.as<float>();
    } else if (jsonTransition.is<size_t>()) {
      transition = jsonTransition.as<size_t>();
    } else {
      DebugSerial.println(F("MiLightClient - WARN: unsupported transition type.  Must be float or int."));
    }
  }

  JsonVariant brightness = request[GroupStateFieldNames::BRIGHTNESS];
  JsonVariant level = request[GroupStateFieldNames::LEVEL];
  const bool isBrightnessDefined = !brightness.isNull() || !level.isNull();

  // Always turn on first
  if (parsedStatus == ON) {
    if (transition == 0) {
      this->updateStatus(ON);
    }
    // Don't do an "On" transition if the bulb is already on.  The reasons for this are:
    //   * Ambiguous what the behavior should be.  Should it ramp to full brightness?
    //   * HomeAssistant is only capable of sending transitions via the `light.turn_on`
    //     service call, which ends up sending `{"status":"ON"}`.  So transitions which
    //     have nothing to do with the status will include an "ON" command.
    // If the user wants to transition brightness, they can just specify a brightness in
    // the same command.  This avoids the need to make arbitrary calls on what the
    // behavior should be.
    else if (!currentState->isSetState() || !currentState->isOn()) {
      // If a brightness is defined, we'll want to transition to that.  Status
      // transitions only ramp up/down to the max/min.  Otherwise, just turn the bulb on
      // and let field transitions handle the rest.
      if (!isBrightnessDefined) {
        handleTransition(GroupStateField::STATUS, status, transition, 0);
      } else {
        this->updateStatus(ON);

        if (! brightness.isNull()) {
          handleTransition(GroupStateField::BRIGHTNESS, brightness, transition, 0);
        } else if (! level.isNull()) {
          handleTransition(GroupStateField::LEVEL, level, transition, 0);
        }
      }
    }
  }

  for (const char* fieldName : FIELD_ORDERINGS) {
    if (request.containsKey(fieldName)) {
      JsonVariant value = request[fieldName];

      // Find handler by linear scan
      const FieldSetter* handler = nullptr;
      for (size_t i = 0; i < NUM_FIELD_SETTERS; i++) {
        if (strcmp(fieldName, FIELD_SETTERS[i].name) == 0) {
          handler = &FIELD_SETTERS[i];
          break;
        }
      }

      if (handler != nullptr) {
        // No transition -- set field directly
        if (transition == 0) {
          handler->handler(this, value);
        } else {
          GroupStateField field = GroupStateFieldHelpers::getFieldByName(fieldName);

          if (   !GroupStateFieldHelpers::isBrightnessField(field)  // If field isn't brightness
               || parsedStatus == STATUS_UNDEFINED                  // or if there was not a status field
               || currentState->isOn()                              // or if bulb was already on
          ) {
            handleTransition(field, value, transition);
          }
        }
      }
    }
  }

  // Raw packet command/args
  if (request.containsKey("button_id") && request.containsKey("argument")) {
    this->command(request["button_id"], request["argument"]);
  }

  // Always turn off last
  if (parsedStatus == OFF) {
    if (transition == 0) {
      this->updateStatus(OFF);
    } else {
      handleTransition(GroupStateField::STATUS, status, transition);
    }
  }

  if (this->updateEndHandler) {
    this->updateEndHandler();
  }
}

void MiLightClient::handleCommands(JsonArray commands) {
  if (! commands.isNull()) {
    for (size_t i = 0; i < commands.size(); i++) {
      this->handleCommand(commands[i]);
    }
  }
}

void MiLightClient::handleCommand(JsonVariant command) {
  const char* cmdName = NULL;
  JsonObject args;

  if (command.is<JsonObject>()) {
    JsonObject cmdObj = command.as<JsonObject>();
    cmdName = cmdObj[GroupStateFieldNames::COMMAND].as<const char*>();
    args = cmdObj["args"];
  } else if (command.is<const char*>()) {
    cmdName = command.as<const char*>();
  }

  if (cmdName == NULL) return;

  if (strcmp(cmdName, MiLightCommandNames::UNPAIR) == 0) {
    this->unpair();
  } else if (strcmp(cmdName, MiLightCommandNames::PAIR) == 0) {
    this->pair();
  } else if (strcmp(cmdName, MiLightCommandNames::SET_WHITE) == 0) {
    this->updateColorWhite();
  } else if (strcmp(cmdName, MiLightCommandNames::NIGHT_MODE) == 0) {
    this->enableNightMode();
  } else if (strcmp(cmdName, MiLightCommandNames::LEVEL_UP) == 0) {
    this->increaseBrightness();
  } else if (strcmp(cmdName, MiLightCommandNames::LEVEL_DOWN) == 0) {
    this->decreaseBrightness();
  } else if (strcmp(cmdName, "brightness_up") == 0) {
    this->increaseBrightness();
  } else if (strcmp(cmdName, "brightness_down") == 0) {
    this->decreaseBrightness();
  } else if (strcmp(cmdName, MiLightCommandNames::TEMPERATURE_UP) == 0) {
    this->increaseTemperature();
  } else if (strcmp(cmdName, MiLightCommandNames::TEMPERATURE_DOWN) == 0) {
    this->decreaseTemperature();
  } else if (strcmp(cmdName, MiLightCommandNames::NEXT_MODE) == 0) {
    this->nextMode();
  } else if (strcmp(cmdName, MiLightCommandNames::PREVIOUS_MODE) == 0) {
    this->previousMode();
  } else if (strcmp(cmdName, MiLightCommandNames::MODE_SPEED_DOWN) == 0) {
    this->modeSpeedDown();
  } else if (strcmp(cmdName, MiLightCommandNames::MODE_SPEED_UP) == 0) {
    this->modeSpeedUp();
  } else if (strcmp(cmdName, MiLightCommandNames::TOGGLE) == 0) {
    this->toggleStatus();
  } else if (strcmp(cmdName, MiLightCommandNames::TRANSITION) == 0) {
    JsonDocument fakedoc;
    this->handleTransition(args, fakedoc);
  }
}

void MiLightClient::handleTransition(GroupStateField field, JsonVariant value, float duration, int16_t startValue) {
  BulbId bulbId = currentRemote->packetFormatter->currentBulbId();
  std::shared_ptr<Transition::Builder> transitionBuilder = nullptr;

  if (currentState == nullptr) {
    DebugSerial.println(F("Error planning transition: could not find current bulb state."));
    return;
  }

  if (!currentState->isSetField(field) && startValue == FETCH_VALUE_FROM_STATE) {
    // Field not set yet -- default to 0 as start value
    startValue = 0;
  }

  if (field == GroupStateField::COLOR) {
    ParsedColor currentColor = currentState->getColor();
    ParsedColor endColor = ParsedColor::fromJson(value);

    transitionBuilder = transitions.buildColorTransition(
      bulbId,
      currentColor,
      endColor
    );
  } else if (field == GroupStateField::STATUS || field == GroupStateField::STATE) {
    uint8_t startLevel;
    MiLightStatus status = parseMilightStatus(value);

    if (startValue == FETCH_VALUE_FROM_STATE || currentState->isOn()) {
      startLevel = currentState->getBrightness();
    } else {
      startLevel = startValue;
    }

    transitionBuilder = transitions.buildStatusTransition(bulbId, status, startLevel);
  } else {
    uint16_t currentValue;
    uint16_t endValue = value;

    if (startValue == FETCH_VALUE_FROM_STATE || currentState->isOn()) {
      currentValue = currentState->getParsedFieldValue(field);
    } else {
      currentValue = startValue;
    }

    transitionBuilder = transitions.buildFieldTransition(
      bulbId,
      field,
      currentValue,
      endValue
    );
  }

  if (transitionBuilder == nullptr) {
    DebugSerial.printf("Unsupported transition field: %s\n", GroupStateFieldHelpers::getFieldName(field));
    return;
  }

  transitionBuilder->setDuration(duration);
  transitions.addTransition(transitionBuilder->build());
}

bool MiLightClient::handleTransition(JsonObject args, JsonDocument& responseObj) {
  if (! args.containsKey(FPSTR(TransitionParams::FIELD))
    || ! args.containsKey(FPSTR(TransitionParams::END_VALUE))) {
    responseObj[F("error")] = F("Ignoring transition missing required arguments");
    return false;
  }

  const BulbId& bulbId = currentRemote->packetFormatter->currentBulbId();
  const char* fieldName = args[FPSTR(TransitionParams::FIELD)];
  JsonVariant startValue = args[FPSTR(TransitionParams::START_VALUE)];
  JsonVariant endValue = args[FPSTR(TransitionParams::END_VALUE)];
  GroupStateField field = GroupStateFieldHelpers::getFieldByName(fieldName);
  std::shared_ptr<Transition::Builder> transitionBuilder = nullptr;

  if (field == GroupStateField::UNKNOWN) {
    char errorMsg[30];
    snprintf_P(errorMsg, sizeof(errorMsg), PSTR("Unknown transition field: %s"), fieldName);
    responseObj[F("error")] = errorMsg;
    return false;
  }

  // These fields can be transitioned directly.
  switch (field) {
    case GroupStateField::HUE:
    case GroupStateField::SATURATION:
    case GroupStateField::BRIGHTNESS:
    case GroupStateField::LEVEL:
    case GroupStateField::KELVIN:
    case GroupStateField::COLOR_TEMP:

      transitionBuilder = transitions.buildFieldTransition(
        bulbId,
        field,
        startValue.isNull()
          ? currentState->getParsedFieldValue(field)
          : startValue.as<uint16_t>(),
        endValue
      );
      break;

    default:
      break;
  }

  // Color can be decomposed into hue/saturation and these can be transitioned separately
  if (field == GroupStateField::COLOR) {
    ParsedColor _startValue = startValue.isNull()
      ? currentState->getColor()
      : ParsedColor::fromJson(startValue);
    ParsedColor endColor = ParsedColor::fromJson(endValue);

    if (! _startValue.success) {
      responseObj[F("error")] = F("Transition - error parsing start color");
      return false;
    }
    if (! endColor.success) {
      responseObj[F("error")] = F("Transition - error parsing end color");
      return false;
    }

    transitionBuilder = transitions.buildColorTransition(
      bulbId,
      _startValue,
      endColor
    );
  }

  // Status is handled a little differently
  if (field == GroupStateField::STATUS || field == GroupStateField::STATE) {
    MiLightStatus toStatus = parseMilightStatus(endValue);
    uint8_t startLevel;
    if (currentState->isSetBrightness()) {
      startLevel = currentState->getBrightness();
    } else if (toStatus == ON) {
      startLevel = 0;
    } else {
      startLevel = 100;
    }

    transitionBuilder = transitions.buildStatusTransition(bulbId, toStatus, startLevel);
  }

  if (transitionBuilder == nullptr) {
    char errorMsg[60];
    snprintf_P(errorMsg, sizeof(errorMsg), PSTR("Unsupported transition field: %s"), fieldName);
    responseObj[F("error")] = errorMsg;
    return false;
  }

  if (args.containsKey(FPSTR(TransitionParams::DURATION))) {
    transitionBuilder->setDuration(args[FPSTR(TransitionParams::DURATION)]);
  }
  if (args.containsKey(FPSTR(TransitionParams::PERIOD))) {
    transitionBuilder->setPeriod(args[FPSTR(TransitionParams::PERIOD)]);
  }

  transitions.addTransition(transitionBuilder->build());
  return true;
}

void MiLightClient::handleEffect(const String& effect) {
  if (effect == MiLightCommandNames::NIGHT_MODE) {
    this->enableNightMode();
  } else if (effect == "white" || effect == "white_mode") {
    this->updateColorWhite();
  } else { // assume we're trying to set mode
    this->updateMode(effect.toInt());
  }
}

JsonVariant MiLightClient::extractStatus(JsonObject object) {
  JsonVariant status;

  if (object.containsKey(FPSTR(GroupStateFieldNames::STATUS))) {
    return object[FPSTR(GroupStateFieldNames::STATUS)];
  } else {
    return object[FPSTR(GroupStateFieldNames::STATE)];
  }
}

uint8_t MiLightClient::parseStatus(JsonVariant val) {
  if (val.isNull()) {
    return STATUS_UNDEFINED;
  }

  return parseMilightStatus(val);
}

void MiLightClient::setRepeatsOverride(size_t repeats) {
  this->repeatsOverride = repeats;
}

void MiLightClient::clearRepeatsOverride() {
  this->repeatsOverride = PacketSender::DEFAULT_PACKET_SENDS_VALUE;
}

void MiLightClient::flushPacket() {
  PacketStream& stream = currentRemote->packetFormatter->buildPackets();

  while (stream.hasNext()) {
    packetSender.enqueue(stream.next(), currentRemote, repeatsOverride);
  }

  currentRemote->packetFormatter->reset();
}

void MiLightClient::onUpdateBegin(EventHandler handler) {
  this->updateBeginHandler = handler;
}

void MiLightClient::onUpdateEnd(EventHandler handler) {
  this->updateEndHandler = handler;
}
