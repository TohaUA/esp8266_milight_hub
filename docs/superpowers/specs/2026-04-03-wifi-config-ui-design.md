# WiFi Configuration in UI and Backup

**Date:** 2026-04-03
**Status:** Approved

## Overview

Add WiFi network configuration to the web UI: dual-WiFi with ordered failover, read-only connection status, DNS server field, and portal resilience against deauth attacks. All new fields participate in backup/restore automatically.

## 1. New Settings Fields

| Key | Type | Default | Max Length | Description |
|-----|------|---------|------------|-------------|
| `wifi_ssid` | String | `""` | 32 | Primary WiFi SSID |
| `wifi_password` | String | `""` | 63 | Primary WiFi password |
| `wifi_ssid_secondary` | String | `""` | 32 | Fallback WiFi SSID |
| `wifi_password_secondary` | String | `""` | 63 | Fallback WiFi password |
| `wifi_dns` | String | `""` | 15 | DNS server IP. Empty = use gateway |
| `wifi_portal_on_fail` | bool | `true` | - | Enter captive portal on boot if both SSIDs fail. When `false`, retry forever |

### Password Handling

`wifi_password` and `wifi_password_secondary` follow the same redaction pattern as existing passwords:
- `serialize()` returns `"***"` when the field is non-empty, `""` when empty
- `patch()` skips the field if incoming value is `"***"`
- UI renders with `type="password"`

### Validation

- SSID: max 32 chars
- Password: max 63 chars (WPA2 limit)
- DNS: valid IPv4 or empty string (same format as `wifi_static_ip`)
- `wifi_ssid_secondary` without `wifi_ssid` is a validation error

## 2. Connection Status (Read-Only)

### Endpoint Changes

Extend the existing `/about` JSON response with:

```json
{
  "wifi_ssid": "MyNetwork",
  "wifi_rssi": -67,
  "wifi_ip": "192.168.1.50",
  "wifi_gateway": "192.168.1.1",
  "wifi_subnet": "255.255.255.0",
  "wifi_dns": "192.168.1.1",
  "wifi_mac": "AA:BB:CC:DD:EE:FF"
}
```

These are runtime values read from `WiFi.SSID()`, `WiFi.RSSI()`, `WiFi.localIP()`, `WiFi.gatewayIP()`, `WiFi.subnetMask()`, `WiFi.dnsIP()`, `WiFi.macAddress()`.

### UI

New **"Connection Status"** block at the top of the Network settings tab, above Security and WiFi sections. Displays the fields above as read-only text. Fetched once on page load from `/about`. No auto-refresh.

## 3. WiFi Connection Logic

### Boot Sequence

```
1. If wifi_ssid is non-empty:
     Try wifi_ssid + wifi_password for 20s
     If connected → done (WIFI_STA mode)
2. If wifi_ssid_secondary is non-empty:
     Try wifi_ssid_secondary + wifi_password_secondary for 20s
     If connected → done (WIFI_STA mode)
3. If wifi_portal_on_fail is true:
     Enter WiFiManager captive portal (timeout 180s)
     If portal times out → restart
4. If wifi_portal_on_fail is false:
     Loop back to step 1, retry indefinitely
```

### Static IP Application

If `wifi_static_ip` is non-empty, call `WiFi.config(ip, gateway, subnet, dns)` before each `WiFi.begin()` attempt. The `wifi_dns` field is passed as the 4th argument; if empty, it is omitted (defaults to gateway).

### Runtime Reconnection (replaces current 30s reconnect loop)

```
On WiFi disconnect (while device is initialized and running):
1. Retry current SSID 3 times (5s apart)
2. If still disconnected, try the other SSID (20s timeout)
3. If both fail, loop back to step 1
4. Never enter portal mode during runtime — only on cold boot
```

This makes the device resilient to deauth attacks: it retries forever during operation.

### Migration from WiFiManager Credentials

On first boot after firmware upgrade, if `wifi_ssid` is empty:
1. Read `WiFi.SSID()` and `WiFi.psk()` (WiFiManager stores these in ESP flash)
2. If SSID is non-empty, populate `wifi_ssid` and `wifi_password` in settings (password may be empty for open networks)
3. Save settings
4. Continue boot with the migrated credentials

This ensures existing devices configured via captive portal continue working without user intervention.

### WiFi Credential Changes via API

When `wifi_ssid`, `wifi_password`, `wifi_ssid_secondary`, `wifi_password_secondary`, `wifi_static_ip`, `wifi_static_ip_gateway`, `wifi_static_ip_netmask`, `wifi_dns`, or `wifi_portal_on_fail` change via `PUT /settings`:
- Save settings to flash
- Restart the device

Note: `wifi_mode` changes are applied live via `WiFi.setPhyMode()` in `applySettings()` and do NOT require a restart.

The frontend shows a confirmation dialog before submitting WiFi credential changes: "Device will restart to apply WiFi changes. Continue?"

Other settings in the same PUT request are saved before the restart, so they take effect too.

## 4. DNS Server Field

Single DNS server field (`wifi_dns`). Applied as the 4th argument to `WiFi.config()` when static IP is configured. When empty, DNS defaults to the gateway IP (current behavior, unchanged).

Shown in the WiFi section of the Network tab, between the netmask and WiFi mode fields.

## 5. UI Layout

### Network Tab

```
--- Connection Status ---
  SSID:       MyNetwork
  IP:         192.168.1.50
  Gateway:    192.168.1.1
  Subnet:     255.255.255.0
  DNS:        192.168.1.1
  MAC:        AA:BB:CC:DD:EE:FF
  Signal:     -67 dBm

--- Security ---
  Admin Username    [                    ]
  Admin Password    [••••••••            ]

--- WiFi ---
  SSID                    [MyNetwork           ]
  Password                [••••••••            ]
  Fallback SSID           [BackupNetwork       ]
  Fallback Password       [••••••••            ]
  Hostname                [milight-hub         ]
  Static IP               [                    ]
  Static IP Gateway       [                    ]
  Static IP Netmask       [                    ]
  DNS Server              [                    ]
  WiFi Mode               [B] [G] [N]
  Portal on Fail          [Enabled] [Disabled]
```

### Confirmation Dialog

WiFi-related field changes (`wifi_ssid`, `wifi_password`, `wifi_ssid_secondary`, `wifi_password_secondary`) trigger a confirmation dialog before saving:

> "Changing WiFi credentials will restart the device. If the new credentials are wrong, the device may become unreachable. Continue?"

Static IP / DNS / portal changes also restart but are lower risk — same dialog without the "unreachable" warning.

## 6. Backup

No special work. All six new fields are standard `Settings` fields:
- Included in `serialize()` output (passwords redacted)
- Restored via `patch()` (redacted passwords skipped)
- Backup export/import works automatically

## 7. Files Changed

| File | Changes |
|------|---------|
| `lib/Settings/Settings.h` | 6 new fields, SettingsKeys constants, defaults |
| `lib/Settings/Settings.cpp` | serialize (with password redaction), patch (with sentinel skip), validate (lengths, secondary requires primary) |
| `src/main.cpp` | Replace WiFiManager-driven connect with settings-driven dual-WiFi boot sequence; migration from WiFiManager credentials; new reconnect logic with failover; apply DNS in static IP config |
| `lib/WebServer/MiLightHttpServer.cpp` | Add wifi_ssid, wifi_rssi, wifi_ip, wifi_gateway, wifi_subnet, wifi_dns, wifi_mac to `/about` response |
| `web/src/pages/settings/section-network.tsx` | Connection Status block (read-only, from /about); new form fields; confirmation dialog for WiFi changes |
| `web/api/api-zod.ts` | Add 6 new fields to Settings Zod schema |
| `web/api/api.ts` | No changes needed (PUT /settings already handles arbitrary fields) |

## 8. Constraints

- Static IP + DNS changes require device restart (same as current behavior)
- WiFi SSID/password changes require device restart
- `WiFi.begin()` blocks the main loop for ~5-10s per attempt — light commands stall during WiFi transitions. Unavoidable on ESP8266.
- ESP8266 has no concurrent STA+STA mode — can only try one network at a time
- Max SSID: 32 chars. Max password: 63 chars (WPA2 spec).
- The WiFiManager library is still used for the captive portal path, but no longer drives the primary connection logic
