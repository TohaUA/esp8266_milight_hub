#include <GroupStateField.h>
#include <Size.h>

namespace GroupStateFieldNames {
  const char UNKNOWN[] = "unknown";
  const char STATE[] = "state";
  const char STATUS[] = "status";
  const char BRIGHTNESS[] = "brightness";
  const char LEVEL[] = "level";
  const char HUE[] = "hue";
  const char SATURATION[] = "saturation";
  const char COLOR[] = "color";
  const char MODE[] = "mode";
  const char KELVIN[] = "kelvin";
  const char TEMPERATURE[] = "temperature";
  const char COLOR_TEMP[] = "color_temp";
  const char BULB_MODE[] = "bulb_mode";
  const char COMPUTED_COLOR[] = "computed_color";
  const char EFFECT[] = "effect";
  const char DEVICE_ID[] = "device_id";
  const char GROUP_ID[] = "group_id";
  const char DEVICE_TYPE[] = "device_type";
  const char OH_COLOR[] = "oh_color";
  const char HEX_COLOR[] = "hex_color";
  const char COMMAND[] = "command";
  const char COMMANDS[] = "commands";
  const char COLOR_MODE[] = "color_mode";
}

static const char* STATE_NAMES[] = {
  GroupStateFieldNames::UNKNOWN,
  GroupStateFieldNames::STATE,
  GroupStateFieldNames::STATUS,
  GroupStateFieldNames::BRIGHTNESS,
  GroupStateFieldNames::LEVEL,
  GroupStateFieldNames::HUE,
  GroupStateFieldNames::SATURATION,
  GroupStateFieldNames::COLOR,
  GroupStateFieldNames::MODE,
  GroupStateFieldNames::KELVIN,
  GroupStateFieldNames::COLOR_TEMP,
  GroupStateFieldNames::BULB_MODE,
  GroupStateFieldNames::COMPUTED_COLOR,
  GroupStateFieldNames::EFFECT,
  GroupStateFieldNames::DEVICE_ID,
  GroupStateFieldNames::GROUP_ID,
  GroupStateFieldNames::DEVICE_TYPE,
  GroupStateFieldNames::OH_COLOR,
  GroupStateFieldNames::HEX_COLOR,
  GroupStateFieldNames::COLOR_MODE,
};

GroupStateField GroupStateFieldHelpers::getFieldByName(const char* name) {
  for (size_t i = 0; i < size(STATE_NAMES); i++) {
    if (0 == strcmp(name, STATE_NAMES[i])) {
      return static_cast<GroupStateField>(i);
    }
  }
  return GroupStateField::UNKNOWN;
}

const char* GroupStateFieldHelpers::getFieldName(GroupStateField field) {
  for (size_t i = 0; i < size(STATE_NAMES); i++) {
    if (field == static_cast<GroupStateField>(i)) {
      return STATE_NAMES[i];
    }
  }
  return STATE_NAMES[0];
}

bool GroupStateFieldHelpers::isBrightnessField(GroupStateField field) {
  switch (field) {
    case GroupStateField::BRIGHTNESS:
    case GroupStateField::LEVEL:
      return true;
    default:
      return false;
  }
}