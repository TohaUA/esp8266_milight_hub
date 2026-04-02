"""
PlatformIO pre-build script: patch RF24_config.h to prevent printf_P macro leak.

RF24 >=1.4 unconditionally defines `#define printf_P Serial.printf` on ESP
platforms. This macro leaks into all other libraries via PlatformIO's global
include path, breaking any library that calls printf_P on non-Serial objects
(e.g., RichHttpServer's `_currentClient.printf_P()`).

Fix: replace the Serial.printf redefinition with a proper printf wrapper
that doesn't reference Serial as a member.
"""

import os

Import("env")

def patch_rf24_config():
    libdeps_dir = os.path.join(env["PROJECT_LIBDEPS_DIR"], env["PIOENV"])
    config_path = os.path.join(libdeps_dir, "RF24", "RF24_config.h")

    if not os.path.exists(config_path):
        return

    with open(config_path, "r") as f:
        content = f.read()

    marker = "// [patched by esp8266_milight_hub]"
    if marker in content:
        return

    # Replace `#define printf_P Serial.printf` with a version that
    # works as a standalone function-like macro instead of a member access.
    # printf() on ESP8266/ESP32 already supports PROGMEM format strings,
    # so we just redirect printf_P to printf.
    old = "#define printf_P Serial.printf"
    new = "#define printf_P printf " + marker

    count = content.count(old)
    if count > 0:
        content = content.replace(old, new)
        with open(config_path, "w") as f:
            f.write(content)
        print("  [patch_rf24] Patched %d printf_P define(s) in RF24_config.h" % count)
    else:
        print("  [patch_rf24] RF24_config.h: printf_P define not found (already fixed upstream?)")

patch_rf24_config()
