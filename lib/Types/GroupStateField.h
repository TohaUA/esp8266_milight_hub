#ifndef _GROUP_STATE_FIELDS_H
#define _GROUP_STATE_FIELDS_H

namespace GroupStateFieldNames {
  extern const char UNKNOWN[];
  extern const char STATE[];
  extern const char STATUS[];
  extern const char BRIGHTNESS[];
  extern const char LEVEL[];
  extern const char HUE[];
  extern const char SATURATION[];
  extern const char COLOR[];
  extern const char MODE[];
  extern const char KELVIN[];
  extern const char TEMPERATURE[];
  extern const char COLOR_TEMP[];
  extern const char BULB_MODE[];
  extern const char COMPUTED_COLOR[];
  extern const char EFFECT[];
  extern const char DEVICE_ID[];
  extern const char GROUP_ID[];
  extern const char DEVICE_TYPE[];
  extern const char OH_COLOR[];
  extern const char HEX_COLOR[];
  extern const char COMMAND[];
  extern const char COMMANDS[];
  extern const char COLOR_MODE[];
};

enum class GroupStateField {
  UNKNOWN,
  STATE,
  STATUS,
  BRIGHTNESS,
  LEVEL,
  HUE,
  SATURATION,
  COLOR,
  MODE,
  KELVIN,
  COLOR_TEMP,
  BULB_MODE,
  COMPUTED_COLOR,
  EFFECT,
  DEVICE_ID,
  GROUP_ID,
  DEVICE_TYPE,
  OH_COLOR,
  HEX_COLOR,
  COLOR_MODE,
};

class GroupStateFieldHelpers {
public:
  static const char* getFieldName(GroupStateField field);
  static GroupStateField getFieldByName(const char* name);
  static bool isBrightnessField(GroupStateField field);
};

#endif
