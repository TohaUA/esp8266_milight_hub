#pragma once

#include <Arduino.h>

namespace ColorUtils {

// Convert RGB (0-255) to HSV (h: 0-1, s: 0-1, v: 0-1)
inline void rgbToHsv(uint8_t r, uint8_t g, uint8_t b, double hsv[3]) {
  double rd = r / 255.0;
  double gd = g / 255.0;
  double bd = b / 255.0;
  double maxVal = max(rd, max(gd, bd));
  double minVal = min(rd, min(gd, bd));
  double d = maxVal - minVal;

  hsv[2] = maxVal;
  hsv[1] = (maxVal == 0) ? 0 : d / maxVal;

  if (maxVal == minVal) {
    hsv[0] = 0;
  } else if (maxVal == rd) {
    hsv[0] = fmod((gd - bd) / d + 6.0, 6.0) / 6.0;
  } else if (maxVal == gd) {
    hsv[0] = ((bd - rd) / d + 2.0) / 6.0;
  } else {
    hsv[0] = ((rd - gd) / d + 4.0) / 6.0;
  }
}

// Convert HSV (h: 0-1, s: 0-1, v: 0-1) to RGB (0-255)
inline void hsvToRgb(double h, double s, double v, uint8_t rgb[3]) {
  if (s == 0) {
    rgb[0] = rgb[1] = rgb[2] = (uint8_t)(v * 255);
    return;
  }

  double hh = h * 6.0;
  if (hh >= 6.0) hh = 0;
  int i = (int)hh;
  double f = hh - i;
  double p = v * (1.0 - s);
  double q = v * (1.0 - s * f);
  double t = v * (1.0 - s * (1.0 - f));

  double r, g, b;
  switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }

  rgb[0] = (uint8_t)(r * 255);
  rgb[1] = (uint8_t)(g * 255);
  rgb[2] = (uint8_t)(b * 255);
}

} // namespace ColorUtils
