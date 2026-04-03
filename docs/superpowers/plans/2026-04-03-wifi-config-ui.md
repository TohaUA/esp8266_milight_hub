# WiFi Configuration UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add WiFi SSID/password configuration, dual-WiFi failover, connection status display, and DNS server field to the web UI and settings backend.

**Architecture:** Six new settings fields (`wifi_ssid`, `wifi_password`, `wifi_ssid_secondary`, `wifi_password_secondary`, `wifi_dns`, `wifi_portal_on_fail`) are added to the existing `Settings` class. The boot sequence in `main.cpp` is replaced from WiFiManager-driven `autoConnect()` to settings-driven `WiFi.begin()` with ordered failover. The `/about` endpoint gains WiFi status fields. The frontend Network settings tab gets a Connection Status block and the new form fields.

**Tech Stack:** C++ (Arduino/ESP8266/ESP32), ArduinoJson v7, WiFiManager, React/TypeScript, Zod, react-hook-form

**Spec:** `docs/superpowers/specs/2026-04-03-wifi-config-ui-design.md`

---

### Task 1: Add new settings fields to Settings.h

**Files:**
- Modify: `lib/Settings/Settings.h:86-135` (SettingsKeys namespace)
- Modify: `lib/Settings/Settings.h:140-180` (constructor defaults)
- Modify: `lib/Settings/Settings.h:212-260` (public member fields)

- [ ] **Step 1: Add SettingsKeys constants**

Add after line 118 (`WIFI_STATIC_IP_NETMASK`):

```cpp
  static const char WIFI_SSID[] PROGMEM = "wifi_ssid";
  static const char WIFI_PASSWORD[] PROGMEM = "wifi_password";
  static const char WIFI_SSID_SECONDARY[] PROGMEM = "wifi_ssid_secondary";
  static const char WIFI_PASSWORD_SECONDARY[] PROGMEM = "wifi_password_secondary";
  static const char WIFI_DNS[] PROGMEM = "wifi_dns";
  static const char WIFI_PORTAL_ON_FAIL[] PROGMEM = "wifi_portal_on_fail";
```

- [ ] **Step 2: Add default values in constructor**

Add after line 177 (`wifiMode(WifiMode::G),`):

```cpp
    wifiSsid(""),
    wifiPassword(""),
    wifiSsidSecondary(""),
    wifiPasswordSecondary(""),
    wifiDns(""),
    wifiPortalOnFail(true),
```

- [ ] **Step 3: Add public member fields**

Add after line 253 (`String wifiStaticIPGateway;`):

```cpp
  String wifiSsid;
  String wifiPassword;
  String wifiSsidSecondary;
  String wifiPasswordSecondary;
  String wifiDns;
  bool wifiPortalOnFail;
```

- [ ] **Step 4: Build to verify compilation**

Run: `pio run -e d1_mini 2>&1 | tail -5`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add lib/Settings/Settings.h
git commit -m "feat(settings): add wifi_ssid, wifi_password, wifi_dns, wifi_portal_on_fail fields"
```

---

### Task 2: Add validation, serialization, and deserialization for new fields

**Files:**
- Modify: `lib/Settings/Settings.cpp:124-148` (validate method)
- Modify: `lib/Settings/Settings.cpp:316-358` (patch method)
- Modify: `lib/Settings/Settings.cpp:567-612` (serialize method)

- [ ] **Step 1: Add validation rules**

In `Settings::validate()`, add after line 140 (`WIFI_STATIC_IP_NETMASK` validation):

```cpp
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
```

- [ ] **Step 2: Add serialization with password redaction**

In `Settings::serialize()`, add after line 608 (`WIFI_STATIC_IP_NETMASK`):

```cpp
  root[FPSTR(SettingsKeys::WIFI_SSID)] = this->wifiSsid;
  root[FPSTR(SettingsKeys::WIFI_PASSWORD)] = this->wifiPassword.length() > 0 ? "***" : "";
  root[FPSTR(SettingsKeys::WIFI_SSID_SECONDARY)] = this->wifiSsidSecondary;
  root[FPSTR(SettingsKeys::WIFI_PASSWORD_SECONDARY)] = this->wifiPasswordSecondary.length() > 0 ? "***" : "";
  root[FPSTR(SettingsKeys::WIFI_DNS)] = this->wifiDns;
  root[FPSTR(SettingsKeys::WIFI_PORTAL_ON_FAIL)] = this->wifiPortalOnFail;
```

- [ ] **Step 3: Add deserialization in patch() with password sentinel skip**

In `Settings::patch()`, add after line 354 (`WIFI_STATIC_IP_NETMASK`):

```cpp
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
```

- [ ] **Step 4: Build to verify compilation**

Run: `pio run -e d1_mini 2>&1 | tail -5`
Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git add lib/Settings/Settings.cpp
git commit -m "feat(settings): validate, serialize, and deserialize wifi config fields"
```

---

### Task 3: Add WiFi status fields to /about endpoint

**Files:**
- Modify: `lib/Settings/AboutHelper.cpp:35-79` (generateAboutObject method)

- [ ] **Step 1: Add WiFi status fields to about response**

In `AboutHelper::generateAboutObject()`, add after line 38 (`ip_address`):

```cpp
  obj[FPSTR("wifi_ssid")] = WiFi.SSID();
  obj[FPSTR("wifi_rssi")] = WiFi.RSSI();
  obj[FPSTR("wifi_gateway")] = WiFi.gatewayIP().toString();
  obj[FPSTR("wifi_subnet")] = WiFi.subnetMask().toString();
  obj[FPSTR("wifi_dns")] = WiFi.dnsIP().toString();
  obj[FPSTR("wifi_mac")] = WiFi.macAddress();
```

Note: `ip_address` already exists at line 38. `WiFi.h` is already included at lines 6-11.

- [ ] **Step 2: Build to verify compilation**

Run: `pio run -e d1_mini 2>&1 | tail -5`
Expected: `SUCCESS`

- [ ] **Step 3: Commit**

```bash
git add lib/Settings/AboutHelper.cpp
git commit -m "feat(about): add wifi_ssid, wifi_rssi, wifi_gateway, wifi_subnet, wifi_dns, wifi_mac to /about"
```

---

### Task 4: Replace WiFiManager boot sequence with settings-driven dual-WiFi

**Files:**
- Modify: `src/main.cpp:44-49` (global WiFiManagerParameter pointers)
- Modify: `src/main.cpp:390-400` (wifiExtraSettingsChange callback)
- Modify: `src/main.cpp:502-597` (setup function)
- Modify: `src/main.cpp:646-653` (reconnect logic in loop)

This is the most complex task. The key change: instead of `wifiManager->autoConnect()` driving the connection, we use `WiFi.begin(ssid, password)` from settings, with WiFiManager only as a fallback portal.

- [ ] **Step 1: Add a helper function for WiFi.config with static IP + DNS**

Add before the `setup()` function (around line 500):

```cpp
/**
 * Apply static IP configuration if wifi_static_ip is set.
 * Call before each WiFi.begin() attempt.
 */
void applyStaticIPConfig() {
  if (settings.wifiStaticIP.length() > 0) {
    IPAddress ip, gw, subnet, dns;
    ip.fromString(settings.wifiStaticIP);
    subnet.fromString(settings.wifiStaticIPNetmask);
    gw.fromString(settings.wifiStaticIPGateway);

    if (settings.wifiDns.length() > 0) {
      dns.fromString(settings.wifiDns);
      WiFi.config(ip, gw, subnet, dns);
    } else {
      WiFi.config(ip, gw, subnet);
    }
  }
}
```

- [ ] **Step 2: Add a helper function to attempt connecting to a single SSID**

Add after the function from Step 1:

```cpp
/**
 * Try connecting to a WiFi network. Returns true if connected.
 * Blocks for up to timeoutMs milliseconds.
 */
bool tryConnect(const String& ssid, const String& password, unsigned long timeoutMs = 20000) {
  if (ssid.length() == 0) return false;

  DebugSerial.printf("Trying WiFi: %s\n", ssid.c_str());
  applyStaticIPConfig();
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(100);
    ledStatus->handle();
  }

  if (WiFi.status() == WL_CONNECTED) {
    DebugSerial.printf("Connected to %s (IP: %s)\n", ssid.c_str(), WiFi.localIP().toString().c_str());
    return true;
  }

  DebugSerial.printf("Failed to connect to %s\n", ssid.c_str());
  WiFi.disconnect();
  return false;
}
```

- [ ] **Step 3: Add migration function for WiFiManager credentials**

Add after the function from Step 2:

```cpp
/**
 * On first boot after upgrade, migrate WiFiManager-stored credentials
 * into settings fields so the device continues to work.
 */
void migrateWiFiManagerCredentials() {
  if (settings.wifiSsid.length() > 0) return;  // already configured

  String storedSSID = WiFi.SSID();
  if (storedSSID.length() > 0) {
    DebugSerial.printf("Migrating WiFiManager credentials for SSID: %s\n", storedSSID.c_str());
    settings.wifiSsid = storedSSID;
    settings.wifiPassword = WiFi.psk();
    settings.save();
  }
}
```

- [ ] **Step 4: Rewrite setup() WiFi section**

Replace the WiFi setup section in `setup()`. The section to replace starts at line 521 (`// start up the wifi manager`) and ends at line 597 (end of `if (wifiManager->autoConnect(...))`).

Replace with:

```cpp
  // Migrate credentials from WiFiManager flash storage (one-time on upgrade)
  migrateWiFiManagerCredentials();

  // Attempt settings-driven WiFi connection
  bool connected = false;

  if (settings.wifiSsid.length() > 0 || settings.wifiSsidSecondary.length() > 0) {
    // Dual-WiFi ordered failover: primary -> secondary -> retry or portal
    while (!connected) {
      connected = tryConnect(settings.wifiSsid, settings.wifiPassword);
      if (!connected) {
        connected = tryConnect(settings.wifiSsidSecondary, settings.wifiPasswordSecondary);
      }
      if (!connected) {
        if (settings.wifiPortalOnFail) {
          break;  // fall through to portal
        }
        DebugSerial.println(F("Both WiFi networks failed. Retrying..."));
        delay(5000);
      }
    }
  }

  if (!connected) {
    // No SSIDs configured or both failed with portal enabled — use WiFiManager captive portal
    wifiManager = new WiFiManager();
    wifiManager->setConfigPortalBlocking(false);
    wifiManager->setConnectTimeout(20);
    wifiManager->setConnectRetries(5);

    // Static IP for portal path
    if (settings.wifiStaticIP.length() > 0) {
      IPAddress _ip, _subnet, _gw;
      _ip.fromString(settings.wifiStaticIP);
      _subnet.fromString(settings.wifiStaticIPNetmask);
      _gw.fromString(settings.wifiStaticIPGateway);
      wifiManager->setSTAStaticIPConfig(_ip, _gw, _subnet);
    }

    wifiManager->setConfigPortalTimeout(180);
    wifiManager->setConfigPortalTimeoutCallback([]() {
      ledStatus->continuous(settings.ledModeWifiFailed);
      DebugSerial.println(F("Wifi config portal timed out.  Restarting..."));
      delay(10000);
      ESP.restart();
    });

    String ssid = "ESP" + String(getESPId());
    connected = wifiManager->autoConnect(ssid.c_str(), "milightHub");
  }

  if (connected) {
    ledStatus->continuous(settings.ledModeOperating);
    DebugSerial.println(F("Wifi connected successfully"));
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    postConnectSetup();
  }
```

- [ ] **Step 5: Remove the old WiFiManagerParameter globals and wifiExtraSettingsChange**

Remove lines 46-49 (the `WiFiManagerParameter*` globals):

```cpp
// REMOVE these lines:
WiFiManagerParameter* wifiStaticIP = NULL;
WiFiManagerParameter* wifiStaticIPNetmask = NULL;
WiFiManagerParameter* wifiStaticIPGateway = NULL;
WiFiManagerParameter* wifiMode = NULL;
```

And remove the `wifiExtraSettingsChange()` function (lines 390-400) since WiFiManager portal parameters are no longer used. The WiFiManager `setSaveConfigCallback` and `addParameter` calls were already removed in Step 4.

Keep the `WiFiManager* wifiManager;` global (line 44) since it's still used for the portal fallback path.

- [ ] **Step 6: Rewrite reconnect logic in loop()**

Replace lines 646-653 (the current `else if` reconnect block) with:

```cpp
  else if (initialized && WiFi.getMode() == WIFI_STA && !WiFi.isConnected()) {
    static unsigned long lastReconnectAttempt = 0;
    static uint8_t retryCount = 0;
    static bool onSecondary = false;

    if (millis() - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = millis();

      if (retryCount < 3) {
        // Retry current SSID
        DebugSerial.println(F("WiFi disconnected. Retrying current SSID..."));
        WiFi.reconnect();
        retryCount++;
      } else {
        // Switch to the other SSID
        retryCount = 0;
        onSecondary = !onSecondary;
        const String& ssid = onSecondary ? settings.wifiSsidSecondary : settings.wifiSsid;
        const String& pass = onSecondary ? settings.wifiPasswordSecondary : settings.wifiPassword;

        if (ssid.length() > 0) {
          DebugSerial.printf("Failing over to %s\n", ssid.c_str());
          applyStaticIPConfig();
          WiFi.begin(ssid.c_str(), pass.c_str());
        } else {
          // Other SSID not configured, flip back
          onSecondary = !onSecondary;
          WiFi.reconnect();
        }
      }
    }
  }
```

- [ ] **Step 7: Add restart-on-wifi-change logic to applySettings or settingsSavedHandler**

WiFi credential changes need a restart. The `settingsSavedHandler` callback in `main.cpp` is wired up in `postConnectSetup()`. We need to detect WiFi-related changes.

Read `main.cpp` to find the `settingsSavedHandler` / `applySettings` callback registration, then add before the existing `applySettings()` call in the settings-saved callback:

In the `postConnectSetup()` function, find where `settingsSavedHandler` is set. It calls `applySettings()`. We need to detect WiFi field changes and restart.

The simplest approach: store a snapshot of WiFi fields before `patch()` and compare after. But since `MiLightHttpServer` owns the handler, we need a different approach.

Add a helper to `Settings` class — but simpler: just check if the relevant fields are dirty after `patch()`. Actually, the cleanest approach: in `main.cpp`, compare the settings before/after in the settings-saved callback.

In `main.cpp`, find the lambda where `applySettings` is called after settings save. Add a check:

```cpp
// In postConnectSetup(), find where httpServer is constructed and settingsSavedHandler is set.
// The MiLightHttpServer constructor takes a settingsSavedHandler callback.
// We need to capture the pre-save wifi state and compare.
```

Actually, looking at the architecture: `MiLightHttpServer` calls `settingsSavedHandler` which is a `std::function<void()>` set in `postConnectSetup()`. The simplest approach is to store the pre-change values in a global and check them:

Add global variables near line 53:

```cpp
// Track WiFi settings to detect changes requiring restart
String prevWifiSsid, prevWifiPassword, prevWifiSsidSecondary, prevWifiPasswordSecondary;
String prevWifiStaticIP, prevWifiStaticIPGateway, prevWifiStaticIPNetmask, prevWifiDns;
bool prevWifiPortalOnFail;
```

Add a function to snapshot and check:

```cpp
void snapshotWifiSettings() {
  prevWifiSsid = settings.wifiSsid;
  prevWifiPassword = settings.wifiPassword;
  prevWifiSsidSecondary = settings.wifiSsidSecondary;
  prevWifiPasswordSecondary = settings.wifiPasswordSecondary;
  prevWifiStaticIP = settings.wifiStaticIP;
  prevWifiStaticIPGateway = settings.wifiStaticIPGateway;
  prevWifiStaticIPNetmask = settings.wifiStaticIPNetmask;
  prevWifiDns = settings.wifiDns;
  prevWifiPortalOnFail = settings.wifiPortalOnFail;
}

bool wifiSettingsChanged() {
  return prevWifiSsid != settings.wifiSsid
      || prevWifiPassword != settings.wifiPassword
      || prevWifiSsidSecondary != settings.wifiSsidSecondary
      || prevWifiPasswordSecondary != settings.wifiPasswordSecondary
      || prevWifiStaticIP != settings.wifiStaticIP
      || prevWifiStaticIPGateway != settings.wifiStaticIPGateway
      || prevWifiStaticIPNetmask != settings.wifiStaticIPNetmask
      || prevWifiDns != settings.wifiDns
      || prevWifiPortalOnFail != settings.wifiPortalOnFail;
}
```

Then in the settings-saved callback (inside `postConnectSetup()`), add after `applySettings()`:

```cpp
if (wifiSettingsChanged()) {
  DebugSerial.println(F("WiFi settings changed. Restarting..."));
  delay(1000);
  ESP.restart();
}
```

The flow is: `MiLightHttpServer::handleUpdateSettings()` calls `settings.patch()` then `saveSettings()` then `settingsSavedHandler()`. By the time the handler fires, `settings` already has new values. So we snapshot at boot and after each save, then compare current vs snapshot.

At `main.cpp:473`, change the `onSettingsSaved` registration from:

```cpp
httpServer->onSettingsSaved(applySettings);
```

To:

```cpp
httpServer->onSettingsSaved([]() {
  bool needsRestart = wifiSettingsChanged();
  applySettings();
  snapshotWifiSettings();
  if (needsRestart) {
    DebugSerial.println(F("WiFi settings changed. Restarting..."));
    delay(1000);
    ESP.restart();
  }
});
```

And add `snapshotWifiSettings()` at the end of `setup()`, after the `if (connected)` block completes (after `postConnectSetup()` call).

- [ ] **Step 8: Build to verify compilation**

Run: `pio run -e d1_mini 2>&1 | tail -5`
Expected: `SUCCESS`

- [ ] **Step 9: Also build ESP32 target**

Run: `pio run -e esp32 2>&1 | tail -5`
Expected: `SUCCESS`

- [ ] **Step 10: Commit**

```bash
git add src/main.cpp
git commit -m "feat(wifi): settings-driven dual-WiFi boot with ordered failover and restart-on-change"
```

---

### Task 5: Update Zod schema with new fields

**Files:**
- Modify: `web/api/api-zod.ts:217-256` (About schema)
- Modify: `web/api/api-zod.ts:484-513` (Settings schema)

- [ ] **Step 1: Add WiFi status fields to About schema**

In the `About` object (line 217-256), add after `ip_address` (line 221):

```typescript
    wifi_ssid: z.string().describe("Currently connected WiFi SSID"),
    wifi_rssi: z.number().int().describe("WiFi signal strength in dBm"),
    wifi_gateway: z.string().describe("WiFi gateway IP address"),
    wifi_subnet: z.string().describe("WiFi subnet mask"),
    wifi_dns: z.string().describe("WiFi DNS server IP address"),
    wifi_mac: z.string().describe("WiFi MAC address"),
```

The `About` schema already uses `.partial().passthrough()` at the end, so these are all optional.

- [ ] **Step 2: Add new settings fields to Settings schema**

In the `Settings` object, add after `wifi_static_ip_netmask` (line 494):

```typescript
    wifi_ssid: z
      .string()
      .max(32)
      .describe("Primary WiFi SSID"),
    wifi_password: z
      .string()
      .max(63)
      .describe("Primary WiFi password"),
    wifi_ssid_secondary: z
      .string()
      .max(32)
      .describe("Fallback WiFi SSID (used when primary fails)"),
    wifi_password_secondary: z
      .string()
      .max(63)
      .describe("Fallback WiFi password"),
    wifi_dns: z
      .string()
      .max(15)
      .describe("DNS server IP address. Leave empty to use gateway as DNS."),
    wifi_portal_on_fail: z
      .boolean()
      .describe("Enter WiFi setup portal on boot if both WiFi networks fail to connect. When disabled, retries forever.")
      .default(true),
```

- [ ] **Step 3: Commit**

```bash
git add web/api/api-zod.ts
git commit -m "feat(web): add wifi config and status fields to Zod schemas"
```

---

### Task 6: Update Network settings UI with Connection Status and new fields

**Files:**
- Modify: `web/src/pages/settings/section-network.tsx`

- [ ] **Step 1: Rewrite section-network.tsx**

Replace the entire contents of `section-network.tsx` with:

```tsx
import * as React from "react";
import { NavChildProps } from "@/components/ui/sidebar-pill-nav";
import { FieldSection, FieldSections } from "./form-components";
import { useSettings } from "@/lib/settings";

const ConnectionStatus: React.FC = () => {
  const { about, isLoadingAbout } = useSettings();

  if (isLoadingAbout || !about) {
    return null;
  }

  const fields = [
    { label: "SSID", value: about.wifi_ssid },
    { label: "IP Address", value: about.ip_address },
    { label: "Gateway", value: about.wifi_gateway },
    { label: "Subnet", value: about.wifi_subnet },
    { label: "DNS", value: about.wifi_dns },
    { label: "MAC", value: about.wifi_mac },
    {
      label: "Signal",
      value: about.wifi_rssi != null ? `${about.wifi_rssi} dBm` : undefined,
    },
  ];

  return (
    <div>
      <h2 className="text-2xl font-bold">Connection Status</h2>
      <hr className="my-4" />
      <div className="space-y-2">
        {fields.map(
          ({ label, value }) =>
            value != null && (
              <div key={label} className="flex">
                <strong className="w-40">{label}:</strong>
                <span>{value}</span>
              </div>
            )
        )}
      </div>
    </div>
  );
};

export const NetworkSettings: React.FC<NavChildProps<"network">> = () => (
  <FieldSections>
    <ConnectionStatus />
    <FieldSection
      title="Security"
      fields={["admin_username", "admin_password"]}
      fieldTypes={{
        admin_password: "password",
      }}
    />
    <FieldSection
      title="WiFi"
      fields={[
        "wifi_ssid",
        "wifi_password",
        "wifi_ssid_secondary",
        "wifi_password_secondary",
        "hostname",
        "wifi_static_ip",
        "wifi_static_ip_gateway",
        "wifi_static_ip_netmask",
        "wifi_dns",
        "wifi_mode",
        "wifi_portal_on_fail",
      ]}
      fieldNames={{
        wifi_ssid: "SSID",
        wifi_password: "Password",
        wifi_ssid_secondary: "Fallback SSID",
        wifi_password_secondary: "Fallback Password",
        wifi_static_ip: "Static IP",
        wifi_static_ip_gateway: "Static IP Gateway",
        wifi_static_ip_netmask: "Static IP Netmask",
        wifi_dns: "DNS Server",
        wifi_portal_on_fail: "Portal on Fail",
      }}
      fieldTypes={{
        wifi_password: "password",
        wifi_password_secondary: "password",
      }}
    />
  </FieldSections>
);
```

- [ ] **Step 2: Build the web UI to verify**

Run: `cd web && npm run build 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add web/src/pages/settings/section-network.tsx
git commit -m "feat(web): add connection status block and wifi config fields to Network settings"
```

---

### Task 7: Add confirmation dialog for WiFi credential changes

**Files:**
- Modify: `web/src/pages/settings/settings-index.tsx:51-66` (debouncedOnSubmit)

The settings form auto-saves on field change. WiFi credential changes need a confirmation dialog since they trigger a restart and can make the device unreachable.

- [ ] **Step 1: Add confirmation logic to the submit handler**

In `settings-index.tsx`, replace the `debouncedOnSubmit` callback (lines 51-66) with:

```tsx
  const wifiFields = new Set([
    "wifi_ssid",
    "wifi_password",
    "wifi_ssid_secondary",
    "wifi_password_secondary",
    "wifi_static_ip",
    "wifi_static_ip_gateway",
    "wifi_static_ip_netmask",
    "wifi_dns",
    "wifi_portal_on_fail",
  ]);

  const debouncedOnSubmit = useCallback(
    debounce(() => {
      const update: Partial<Settings> = {};
      const dirtyFields = form.formState.dirtyFields;

      for (const field in dirtyFields) {
        update[field as keyof Settings] = form.getValues(field);
      }

      if (Object.keys(update).length === 0) return;

      const hasWifiChange = Object.keys(update).some((f) => wifiFields.has(f));

      if (hasWifiChange) {
        const confirmed = window.confirm(
          "Changing WiFi settings will restart the device. " +
            "If the new credentials are wrong, the device may become unreachable. Continue?"
        );
        if (!confirmed) {
          form.reset(form.getValues());
          return;
        }
      }

      api.putSettings(update).then(() => {
        form.reset(form.getValues());
      });
    }, 300),
    [form]
  );
```

Note: the `wifiFields` set is defined outside the callback for readability but could also go inside.

- [ ] **Step 2: Build the web UI to verify**

Run: `cd web && npm run build 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add web/src/pages/settings/settings-index.tsx
git commit -m "feat(web): add confirmation dialog for wifi credential changes that trigger restart"
```

---

### Task 8: Build full firmware and web UI, verify everything compiles

**Files:** None (verification only)

- [ ] **Step 1: Build web UI**

Run: `cd web && npm run build 2>&1 | tail -10`
Expected: Build succeeds, `dist/bundle.js.gz.h` and `dist/index.html.gz.h` updated

- [ ] **Step 2: Build ESP8266 firmware (includes web assets)**

Run: `pio run -e d1_mini 2>&1 | tail -10`
Expected: `SUCCESS` — firmware fits within flash limits

- [ ] **Step 3: Build ESP32 firmware**

Run: `pio run -e esp32 2>&1 | tail -10`
Expected: `SUCCESS`

- [ ] **Step 4: Check flash size hasn't exceeded limits**

Review the `RAM` and `Flash` usage lines from the build output. ESP8266 d1_mini has 1MB flash for firmware (1044464 bytes). Current usage is 77% — the new fields add ~200 bytes of strings. Should be fine.

- [ ] **Step 5: Commit built web assets**

```bash
git add dist/bundle.js.gz.h dist/index.html.gz.h
git commit -m "build: update web UI bundle with wifi config features"
```
