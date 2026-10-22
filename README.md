# ESP32 weather aggregator

PlatformIO/Arduino firmware for a Seeed Studio XIAO ESP32-S3 with an Adafruit
1.47-inch 172x320 ST7789 display connected through the EYESPI BFF.

## Current functionality

- Landscape 320x172 status page with an RGB test strip.
- Wi-Fi connection status, SSID, IP address, signal strength, uptime, and free heap.
- Multiple saved networks using the same credentials format as CYD-Weather.
  Asynchronous scans select the strongest visible configured access point.
- Connection attempts time out after 20 seconds and rescan after 15 seconds.
  A lost connection also triggers a rescan after 15 seconds. The display continues
  updating while scanning and waiting for Wi-Fi. Hidden networks are not supported.
  The firmware does not roam away from a working connection.
- NTP synchronization using two configured servers, with local date/time and
  POSIX timezone/DST rules. Initial synchronization times out after 15 seconds;
  retries start 60 seconds after the preceding request. Once synchronized, the
  clock continues offline and the SNTP service periodically resynchronizes.
- USB CDC serial diagnostics at 115200 baud; no wait for a serial monitor at boot.

Weather collection and Adafruit IO publishing are not implemented yet.
[The feed catalog](WEATHER_FEED_CATALOG.csv) remains the migration reference.

## Wiring

These are the EYESPI BFF's default connections, matching the supplied
[CircuitPython example](Board_Setup_CP/code.py):

| Signal | XIAO pin | ESP32-S3 GPIO |
| --- | --- | --- |
| SPI clock | D8 / SCK | 7 |
| SPI MOSI | D10 / MOSI | 9 |
| SPI MISO (SD only) | D9 / MISO | 8 |
| TFT chip select | D6 / TX | 43 |
| TFT data/command | D7 / RX | 44 |
| SD chip select (held high, unused) | D3 / A3 | 4 |

Use the BFF's normal power/ground connections and an appropriate EYESPI cable.
Reset and backlight control are not connected by default; the display uses its
onboard reset circuitry and default-on backlight. No SD card support is enabled.
The ST7789 library calculates the panel's 34-pixel column offset from
`init(172, 320)`. Rotation is set to 3 for landscape. If the assembled display
is upside down, change `setRotation(3)` to `setRotation(1)`.

## Configure and build

1. Copy [include/credentials_template.h](include/credentials_template.h) to
   `include/credentials.h` if the local credentials file does not already exist.
2. Set the SSID/password entries in `wifiNetworks` in that local file.
   Use a 2.4 GHz Wi-Fi network. An empty password supports an open network.
   C++ string literals require escaping embedded quotes and backslashes.
   Set `NTP_SERVER_1`, `NTP_SERVER_2`, and `TZ_STRING` for time synchronization.
   `TZ_STRING` must be a POSIX rule (for example, `CST6CDT,M3.2.0,M11.1.0`
   for US Central), not an IANA name. The template defaults to UTC.
   Adafruit IO placeholders are reserved for the later feed implementation.
3. Run `pio run` or use PlatformIO's Build action.
4. Upload using PlatformIO's Upload action or `pio run --target upload`.
5. Open the serial monitor with `pio device monitor --baud 115200`.

The local credentials header is ignored by Git. Do not commit credentials.
Without it (or with all SSIDs empty), the firmware still builds and displays
configuration instructions without attempting to connect. Existing CircuitPython
settings are not read or copied automatically.

**Uploading replaces the existing firmware, including CircuitPython if present.**
Back up any device-side files before uploading.

## Hardware acceptance checks

After upload, verify that the title and all three RGB blocks are visible without
clipping. With valid credentials, confirm that the page shows an IP address and
RSSI. With multiple configured networks visible, confirm that startup selects
the strongest signal. Turn off the connected access point to check the
lost-connection message and rescan countdown, then confirm connection to the
strongest remaining configured access point. Restore connectivity and confirm
the local time and timezone. Block NTP to check timeout/retry reporting.
With no known SSID visible, confirm that scanning repeats; with an incorrect
password, confirm that connection attempts time out and retry while uptime
continues updating. Rebuild without the credentials header to check the
configuration page.

An SPI display is write-only here: successful driver initialization does not
prove that the panel is physically connected. Visual checks are required.
