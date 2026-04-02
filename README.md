# esp8266_milight_hub [![Build](https://github.com/TohaUA/esp8266_milight_hub/actions/workflows/build.yaml/badge.svg)](https://github.com/TohaUA/esp8266_milight_hub/actions/workflows/build.yaml) [![License][shield-license]][info-license]

This is a fork of [sidoh/esp8266_milight_hub](https://github.com/sidoh/esp8266_milight_hub) — a replacement for a Milight/LimitlessLED WiFi gateway hosted on an ESP8266/ESP32. Leverages [Henryk Plötz's awesome reverse-engineering work](https://hackaday.io/project/5888-reverse-engineering-the-milight-on-air-protocol).

[Milight bulbs](https://www.amazon.com/Mi-light-Dimmable-RGBWW-Spotlight-Smart/dp/B01LPRQ4BK/r) are cheap smart bulbs that are controllable with an undocumented 2.4 GHz protocol. In order to control them, you either need a [remote](https://www.amazon.com/Mi-light-Dimmable-RGBWW-Spotlight-Smart/dp/B01LCSALV6/r?th=1) ($13), which allows you to control them directly, or a [WiFi gateway](http://futlight.com/productlist.aspx?typeid=125) ($30), which allows you to control them with a mobile app or a [UDP protocol](https://github.com/Fantasmos/LimitlessLED-DevAPI).

This project is a replacement for the wifi gateway.

## Features

* Fully-featured Web UI (React 18 + TypeScript + Tailwind)
* MQTT support with HomeAssistant auto-discovery
* MQTT state sync on reconnect (`POST /mqtt/sync`)
* UDP gateway (v5/v6 protocol emulation)
* REST API ([OpenAPI spec](docs/openapi.yaml))
* Server-side tracking of device state
* Passive listening for intercepted packets from other Milight devices
* Full ESP32 support
* Telnet debug console (port 23) with live log streaming
* WiFi auto-reconnect on disconnect
* LittleFS with atomic writes (power-loss safe)
* Heap monitoring with auto-restart safety valve

## Quick Start

### What you'll need

1. An ESP8266 or ESP32. I used a NodeMCU.
2. A NRF24L01+ module (~$3 on ebay). Alternatively, you can use a LT8900.
3. Some way to connect the two (7 female/female dupont cables is probably easiest).

### Wiring Guide

This project technically supports both NRF24L01 and LT8900 radios, but I recommend using an NRF24.  Both modules are SPI devices and should be connected to the standard SPI pins on the ESP8266. See the below diagram for pin connections.

##### NRF24L01+

[This guide](https://www.mysensors.org/build/connect_radio#nrf24l01+-&-esp8266) details how to connect an NRF24 to an ESP8266. By default GPIO 4 for CE and GPIO 15 for CSN are used, but these can be configured later in the Web UI under Settings -> Hardware.

<img src="https://user-images.githubusercontent.com/40266/47967518-67556f00-e05e-11e8-857d-1173a9da955c.png" align="left" width="32%" />
<img src="https://user-images.githubusercontent.com/40266/47967520-691f3280-e05e-11e8-838a-83706df2edb0.png" align="left" width="22%" />

_Image source: [MySensors.org](https://mysensors.org)_

NodeMCU (Esp8266) | Esp32        | Radio | Color
--------- |--------------|----| --
GND | GND          | GND | Black        
3V3 | 3V3          | VCC | Red    
D2 (GPIO4) | D4 (GPIO4)   | CE | Orange 
D8 (GPIO15) | D5 (GPIO5)   | CSN/CS | Yellow 
D5 (GPIO14) | D18 (GPIO18) | SCK | Green 
D7 (GPIO13) | D23 (GPIO23) | MOSI | Blue  
D6 (GPIO12) | D19 (GPIO19) | MISO | Violet 


##### LT8900

Connect SPI pins (CE, SCK, MOSI, MISO) to appropriate SPI pins on the ESP8266. With default settings, connect RST to GPIO 0, PKT to GPIO 16, CE to GPIO 4, and CSN to GPIO 15.  Make sure to properly configure these if using non-default pinouts.

### Install firmware

#### ESP8266

If you have [PlatformIO](http://platformio.org/) set up, you can compile from source and upload with:

```
platformio run -e d1_mini --target upload
```

(make sure to substitute `d1_mini` with the board that you're using.)

Alternatively, you can download a pre-compiled firmware image from the [releases](https://github.com/TohaUA/esp8266_milight_hub/releases). This can be used with [`esptool.py`](https://github.com/espressif/esptool):

```
esptool.py write_flash 0x0 <firmware_file.bin>
```

#### ESP32

For ESP32, you need to flash both the partition table and the firmware image. The easiest way to do this is with [PlatformIO](http://platformio.org/):

```
platformio run -e esp32 --target upload
```

After you've flashed it once, you can update using the web ui or with `esptool.py`:

```
# >>> FOR ESP32 ONLY <<<
esptool.py write_flash 0x1000 <firmware_file.bin>
```

### Configure WiFi

This project uses [WiFiManager](https://github.com/tzapu/WiFiManager) to avoid the need to hardcode AP credentials in the firmware.

When the ESP powers on, you should be able to see a network named "ESPXXXXX", with XXXXX being an identifier for your ESP. Connect to this AP and a window should pop up prompting you to enter WiFi credentials.  If your board has a built-in LED (or you wire up an LED), it will [flash to indicate the status](#led-status).

The network password is "**milightHub**".

#### Get IP Address

Both mDNS and SSDP are supported.

* OS X - you should be able to navigate to http://milight-hub.local.
* Windows - you should see a device called "ESP8266 MiLight Gateway" show up in your network explorer.
* Linux users can install [avahi](http://www.avahi.org/) (`sudo apt-get install avahi-daemon` on Ubuntu), and should then be able to navigate to http://milight-hub.local.

### Use it!

The default hostname is `milight-hub`. If your network supports local DNS, you can navigate to `http://milight-hub` (you may also want to try `http://milight-hub.local` if your client supports mDNS).

The UI should look like this:

![Web UI](https://github.com/user-attachments/assets/95fc6faa-eb08-48bd-a0ea-caff15bd4857)

Add devices using the "+" button. Use the "Sniffer" tab to intercept packets from existing remotes or milight devices if you wish to spoof their device IDs.

More details on this are [in the wiki](https://github.com/TohaUA/esp8266_milight_hub/wiki/Pairing-new-bulbs).

### (Optional) HomeAssistant

Set up HomeAssistant discovery by configuring an MQTT connection with the same broker your HomeAssistant instance is using. If all goes well, lights you create should automatically be discovered by HomeAssistant.

## Supported remotes

The following remotes can be emulated:

Support has been added for the following [bulb types](http://futlight.com/productlist.aspx?typeid=101):

Model #|Name|Compatible Bulbs
-------|-----------|----------------
|FUT096|RGB/W|<ol><li>FUT014</li><li>FUT016</li><li>FUT103</li>|
|FUT005<br/>FUT006<br/>FUT007</li></ol>|CCT|<ol><li>FUT011</li><li>FUT017</li><li>FUT019</li></ol>|
|FUT098|RGB|Most RGB LED Strip Controlers|
|FUT020|RGB|Some other RGB LED strip controllers|
|FUT092|RGB/CCT|<ol><li>FUT012</li><li>FUT013</li><li>FUT014</li><li>FUT015</li><li>FUT103</li><li>FUT104</li><li>FUT105</li><li>Many RGB/CCT LED Strip Controllers</li></ol>|
|FUT091|CCT v2|Most newer dual white bulbs and controllers|
|FUT089|8-zone RGB/CCT|Most newer rgb + dual white bulbs and controllers|
|B05|4-zone RGB/CCT|?|

Other remotes or bulbs, but have not been tested.

If it does not work as expected see [Troubleshooting](https://github.com/TohaUA/esp8266_milight_hub/wiki/Troubleshooting).

## Device Aliases

You can configure aliases or labels for a given _(Device Type, Device ID, Group ID)_ tuple.  For example, you might want to call the RGB+CCT remote with the ID `0x1111` and the Group ID `1` to be called `living_room`.  Aliases are useful in a couple of different ways:

* **In the UI**: These show up as named devices in the UI.
* **In the REST API**: standard CRUD verbs (`GET`, `PUT`, and `DELETE`) allow you to interact with aliases via the `/gateways/:device_alias` route.
* **MQTT**: you can configure topics to listen for commands and publish updates/state using aliases rather than IDs.
* **HomeAssistant**: if you've configured MQTT discovery, aliases that you configure will automatically be discovered by HomeAssistant.

## REST API

API documentation is generated from the [OpenAPI spec](docs/openapi.yaml).

## MQTT

To configure your ESP to integrate with MQTT, fill out the following settings:

1. `mqtt_server`- IP or hostname should work. Specify a port with standard syntax (e.g., "mymqttbroker.com:1884").
1. (if necessary) `mqtt_username` and `mqtt_password`
1. (optional) topic patterns. These come pre-configured with suitable values, but you can customize them if you'd like. These control which topics the device will publish and subscribe to to receive commands and publish updates.

### MQTT State Sync

If MQTT state becomes out of sync with the hub (e.g., after broker restart), you can force a full republish of all known state:

```bash
curl -X POST http://<device-ip>/mqtt/sync
```

State is also automatically synced on every MQTT reconnection.

### Topics

There are a few different types of topics used by the light hub. In most cases, the topics are similar to API routes -- they contain the device identifiers in the topic.

A pattern of the form:

```
milight/commands/:device_id/:device_type/:group_id
```

Will cause the ESP to subscribe to the topic `milight/commands/+/+/+` and interpret the second, third, and fourth tokens as `:device_id`, `:device_type`, and `:group_id`, respectively.

Likewise, a pattern of the form:

```
milight/states/:device_id/:device_type/:group_id
```

will cause the ESP to publish state updates to the topic (for example) 

```
milight/states/0x1234/rgb_cct/1
```

Here's a brief description of supported topics:

1. `mqtt_topic_pattern` - controls the topic that the ESP subscribes to for commands. See the above example.
1. `mqtt_update_topic_pattern` - controls the topic that the ESP publishes delta updates to. These are a fairly direct translation of the raw RF packets submitted by milight control devices. They might look something like `{"state":"ON"}`.
1. `mqtt_state_topic_pattern` - controls the topic that the ESP publishes full state updates to. These are JSON objects that contain the entirety of the current state for a given device. The hub tracks state internally and applies updates as they come in.
1. `mqtt_client_status_topic` - nothing fancy for this one! It controls the topic that the ESP publishes client status updates to (in MQTT lingo, this is where birth and LWT messages are sent). 

### Customize state fields

If you're integrating with a platform that expects specific fields in the state update topic, you can customize which fields are included in the state updates published by the hub. The default is designed to be compatible with HomeAssistant!

There's a fair amount of duplication in the available fields (for example, `computed_color`, `oh_color`, `hex_color` all control the `color` field in state updates). Sorry this is confusing!

## UDP Gateways

If for whatever reason you wish to integrate with this hub using the UDP protocol used by the official Milight gateways, you can do that! 

1. In the Web UI, navigate to Settings -> UDP
1. Add a gateway with the "+" button.
1. Configure the remote ID you want to associate with this gateway. If you're wanting to mimic an existing gateway, use the "Sniffer" tab while the existing gateway is sending commands to snag its ID.
1. Choose a UDP port for the gateway -- it shouldn't matter, maybe choose something in the 5000 range.
1. Choose v5 or v6 based on your integration requirements.

The UDP protocol is documented [in this handy github archive](https://github.com/BKrajancic/LimitlessLED-DevAPI/). Version 6 has support for the newer RGB+CCT bulbs and also includes response packets, which can theoretically improve reliability. Version 5 has much smaller packets and is probably lower latency.

## Transitions

Transitions between two given states are supported.  Depending on how transition commands are being issued, the duration and smoothness of the transition are both configurable.  There are a few ways to use transitions:

#### `transition` field when issuing commands

When you issue a command to a bulb either via REST or MQTT, you can include a `transition` field.  The value of this field specifies the duration of the transition, in seconds (non-integer values are supported).

For example, the command:

```json
{"brightness":255,"transition":60}
```

will transition from whatever the current brightness is to `brightness=255` over 60 seconds.

#### Notes on transitions

* espMH's transitions should work seamlessly with [HomeAssistant's transition functionality](https://www.home-assistant.io/components/light/).
* You can issue commands specifying transitions between many fields at once.  For example:
  ```json
  {"brightness":255,"kelvin":0,"transition":10.5}
  ```
  will transition from current values for brightness and kelvin to the specified values -- 255 and 0 respectively -- over 10.5 seconds.
* Color transitions are supported.  Under the hood, this is treated as a transition between current values for r, g, and b to the r, g, b values for the specified color.  Because milight uses hue-sat colors, this might not behave exactly as you'd expect for all colors.
* You can transition to a given `status` or `state`.  For example,
  ```json
  {"status":"ON","transition":10}
  ```
  will turn the bulb on, immediately set the brightness to 0, and then transition to brightness=255 over 10 seconds.  If you specify a brightness value, the transition will stop there instead of 255.

## Telnet Debug Console

A telnet server runs on port 23. All firmware log output is streamed to connected telnet clients in real time. Interactive commands are available:

```bash
telnet <device-ip> 23
```

| Command | Description |
|---------|-------------|
| `heap` | Show free heap memory |
| `status` | Show full device status JSON |
| `uptime` | Show uptime in milliseconds |
| `sync` | Trigger MQTT state sync |
| `help` | List available commands |

## LED Status

Some ESP boards have a built-in LED, on pin #2.  This LED will flash to indicate the current status of the hub:

* Wifi not configured: Fast flash (on/off once per second).  See [Configure Wifi](#configure-wifi) to configure the hub.
* Wifi connected and ready: Occasional blips of light (a flicker of light every 1.5 seconds).
* Packets sending/receiving: Rapid blips of light for brief periods (three rapid flashes).
* Wifi failed to configure: Solid light.

In the setup UI, you can turn on "enable_solid_led" to change the LED behavior to:

* Wifi connected and ready: Solid LED light
* Wifi failed to configure: Light off

Note that you must restart the hub to affect the change in "enable_solid_led".

You can configure the LED pin from the web console.  Note that pin means the GPIO number, not the D number ... for example, D1 is actually GPIO5 and therefore its pin 5.  If you specify the pin as a negative number, it will invert the LED signal (the built-in LED on pin 2 (D4) is inverted, so the default is -2).

## Development

This project is developed and built using [PlatformIO](https://platformio.org/).

The Web UI is [documented here](./web/README.md). Architecture documentation is available in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

#### Running tests

On-board unit tests are available using PlatformIO. Tests are in `test/test_embedded/` and run on actual hardware:

```
pio test -e nodemcuv2 --upload-port <serial-port> --test-port <serial-port>
```

#### Running integration tests

A remote integration test suite built using rspec is available under [`./test/remote`](test/remote).

## Acknowledgements

This is a fork of [sidoh/esp8266_milight_hub](https://github.com/sidoh/esp8266_milight_hub). Original project by [@sidoh](https://github.com/sidoh).

* @WoodsterDK added support for LT8900 radios.
* @cmidgley contributed many substantial features to the 1.7 release.

[info-license]:   https://github.com/TohaUA/esp8266_milight_hub/blob/main/LICENSE
[shield-license]: https://img.shields.io/badge/license-MIT-blue.svg
