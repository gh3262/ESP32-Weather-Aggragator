# ESP32 weather aggregator

PlatformIO/Arduino firmware for a Seeed Studio XIAO ESP32-S3 with an Adafruit
1.47-inch 172x320 ST7789 display connected through the EYESPI BFF.

## Current functionality

- Landscape 320x172 status page with an RGB test strip.
- Wi-Fi connection status, SSID, IP address, signal strength, uptime, and free heap.
- Multiple saved networks using the same credentials format as CYD-Weather.
  Asynchronous scans rank saved SSIDs by their strongest visible signal.
  Connections use plain `WiFi.begin(ssid, password)` without channel or BSSID
  binding, allowing the driver to choose a satellite/access point.
  Scans wait non-blockingly for completion events with a 20-second deadline,
  rather than polling the wrapper's six-second timeout. Diagnostic scans on
  this board completed successfully in 6.4-6.8 seconds, beyond that old deadline.
- Connection attempts time out after 20 seconds and rescan after 15 seconds.
  A lost connection also triggers a rescan after 15 seconds. The display continues
  updating while scanning and waiting for Wi-Fi. Hidden networks are not supported.
  The firmware does not roam away from a working connection.
  After two failed 20-second connection attempts to a saved network, that entry
  is skipped for 10 minutes and the strongest remaining eligible saved network
  is selected. Failures reset on successful connection or cooldown expiry.
  If none is eligible, discovery continues every 15 seconds until one is.
  Successful scans refresh cached signal information and visibility, combining
  satellites with the same SSID into one saved-network candidate.
  If a scan fails, the failure is logged and the last successful scan's known
  saved networks are used for a best-effort connection (they may have moved or
  disappeared). Scan failures alone do not count as authentication failures.
  Cooldowns are in RAM and reset when the board reboots.
  For hardware validation, let an unavailable/incorrectly configured access
  point reach two failures and check the cooldown log and selection of another
  saved network. Leave it running to verify eligibility returns after 10 minutes.
  Failed scans/connections and lost links cancel the connection and any scan
  before the retry wait, keeping station mode initialized rather than repeatedly
  deinitializing the radio. Timed-out scans are explicitly cancelled before
  cached-network fallback. This recovery path still needs on-board verification.
  USB diagnostics include disconnect reason names/codes, selected credential
  index (not its password), signal strength, channel, scan duration, and scan
  completion event status/result count.
- NTP synchronization using two configured servers, with local date/time and
  POSIX timezone/DST rules. Initial synchronization times out after 15 seconds;
  retries start 60 seconds after the preceding request. Once synchronized, the
  clock continues offline and the SNTP service periodically resynchronizes.
- USB CDC serial diagnostics at 115200 baud; no wait for a serial monitor at boot.
- Eight-pixel RGB NeoPixel ring on D0 (GPIO1), using pixel 0 for network
  status: amber while scanning/connecting, green when connected, and red when
  unconfigured, initialization fails, or waiting to reconnect. Two additional
  pixels show AIO publishing health (1) and WeatherKit reception health (2);
  pixels 3-7 remain off. Brightness is limited to 16 out of 255 in software.
  Enabled using `kEnableStatusPixels = true`; set it to `false` to isolate
  pixel initialization and updates during Wi-Fi diagnostics.

The main firmware receives WeatherKit current conditions, 5-/30-minute
forecasts, and hour-1 forecasts through Adafruit IO MQTT and publishes changed
values through the shared paced queue. Other providers, other hourly horizons,
and daily forecast adapters are not implemented yet.
[The feed catalog](WEATHER_FEED_CATALOG.csv) remains the migration reference.

### Adafruit IO publishing foundation

The application starts its TLS-verified MQTT connection after Wi-Fi and NTP are
ready. Feed samples are submitted in named groups; the queue publishes one
sample at a time, waits for its QoS 1 acknowledgement, and spaces publish
attempts by at least two seconds. This caps new application publishes at 30
per minute, including entries sent to `aggregator.log`, leaving account
capacity for other feeds. MQTT's QoS 1 retransmissions can repeat a publish
after a connection failure; they are not new application queue entries.
The queue is bounded and rejects new groups when it
cannot accept them; callers must report that result rather than silently losing
updates.

The MQTT client retains ownership of an outstanding message across reconnects;
the application does not enqueue another copy on disconnect. If no
acknowledgement arrives within 30 seconds, the application stops and destroys
the client (clearing its old outbox) before reconnecting and retrying the
uncompleted sample. QoS 1 is at-least-once delivery, not exactly-once: if the
broker stored a value but its acknowledgement was lost, retrying can create a
duplicate data point. Group success means every newly queued changed sample
received a broker acknowledgement, not that the group was atomic or that Adafruit IO performed
application-level validation. Account error/throttle notices are reported on
serial.

After every changed sample queued for a group is acknowledged, one concise
success entry for the group is queued to `aggregator.log` (the replacement for the old `rt` log
feed). MQTT connection and publish failures are also reported there when
possible. Detailed development and status output remains on USB serial and is
not forwarded to the log feed. Weather-group selection, failure retry policy,
and log message format can be refined as provider groups are added. Create the
`aggregator.log` feed in Adafruit IO before enabling publishing.

Every operational log-feed value starts with local event time in
`YYYYMMDDHHMMSS` format followed by a space, for example
`20261024110442 Sent feed group WeatherKit current - Number Updated: 7`.
The count is changed updates queued and acknowledged for that group, excluding
unchanged samples, log entries, and transport retries. Repeated transitions to
the same feed within one group count as separate updates.
The prefix uses the
NTP-synchronized clock and configured timezone/DST rules when the event enters
the log queue, not when it is eventually published. Events before synchronization
use `TIME_UNSYNCED` instead. Detailed serial messages are unchanged.

### Change detection

`enqueueGroup()` compares the final formatted value byte-for-byte for each feed.
Adapters must normalize units and precision first; `"72"` and `"72.0"` are
different values. The last acknowledged value is recorded only after the
matching MQTT acknowledgement, never when a sample is accepted or sent.

The newest queued/in-flight value for the feed takes precedence over its
acknowledged value when filtering a new submission. Identical repeats are
skipped, but genuine transitions stay in FIFO order, including a return to an
earlier value (for example, `72 -> 73 -> 72`). Updates are not coalesced.
This also applies to repeated feed keys within one submitted group.

A completely unchanged group returns `true` without using queue space or
creating a success entry in `aggregator.log`; it is reported only on serial.
Mixed groups queue only changed samples and log completion after the last of
those samples is acknowledged. Invalid inputs or insufficient queue/cache
space reject the entire group without mutating the queue or change cache.
The RAM-only cache tracks up to 256 distinct feed keys per boot, with no
eviction. A full cache rejects previously unseen keys explicitly, but existing
keys can still update. Reboot clears it, so the initial values are sent again.
Periodic heartbeat publishing is intentionally deferred. Internal log events
are not change-filtered.

The current API supports ordinary, undated feed values only. Timestamped
historical data will need timestamp-aware payloads and cache identities when
that provider integration is added; it must not use this undated path.

### On-board publisher test

Upload the main `seeed_xiao_esp32s3` environment and open its serial monitor at
115200 baud. After MQTT connects, send `t` (a trailing newline is fine).
The command submits `A,A,B,A` through the normal change filter and publish
queue, targeting only `aggregator.diag`. Existing feed history is not cleared.
For the first test run since boot, expect exactly these three new entries:

```text
publisher-test:1:A
publisher-test:1:B
publisher-test:1:A
```

The duplicate A is suppressed, while the return from B to A is preserved.
Verify the entries' timestamps are at least two seconds apart (other queued
traffic may increase the gap), then check `aggregator.log` for
`Sent feed group publisher-test - Number Updated: 3`.
Acceptance on serial means queued, not yet
delivered; completion is logged only after MQTT acknowledges all three samples.
QoS 1 recovery can duplicate data points after lost acknowledgements, as noted
above. Repeating `t` uses run 2, then run 3, so each run exercises all three
publishes. The run counter resets on reboot. Send `?` to repeat command help.
The test never runs automatically and does not write to real weather feeds.

### WeatherKit current conditions

With `AIO_USERNAME`, `AIO_KEY`, and `AIO_WEATHER_LOCATION_ID` configured, the
main MQTT client subscribes to
`{username}/integration/weather/{location_id}/current` on every connection.
An empty location ID disables only WeatherKit collection, not publishing.
No HTTP startup fetch is performed: expect the first data at the next Power-Up
push, usually within about a minute (occasionally a push is skipped).
Stop the old current-condition feeder before uploading this replacement.

#### Adding, disabling, or changing output feeds

[feed_config.h](include/feed_config.h) is the compile-time source of truth for
weather output mappings. The CSV catalog remains a historical migration
inventory; editing it does not change firmware behavior.

Each `weatherkitCurrent` row specifies `{enabled, source field, destination key,
conversion}`. Set `enabled` to `false` to stop publishing an output, or remove
its row. Add a row to publish another field in the current JSON object, using
an existing conversion; a new conversion needs adapter code and tests.
Changing a destination key redirects future values. Wind degrees and compass
text are independent rows and can be enabled or disabled separately.
All 20 current outputs and all six minute-forecast outputs are enabled by
default. `weatherkitMinutes5` and `weatherkitMinutes30` configure their outputs
independently using the same row format. `weatherkitHour1` independently
configures 21 enabled hour-1 outputs.

Rebuild, run the adapter/publisher tests, and upload after a configuration
change. The default-mapping assertions in the adapter tests intentionally
describe the shipping configuration; update their expected keys/counts/values
when changing that configuration. Disabled fields are neither validated nor
queued. Disabling every output is a serial-reported no-op; it does not stop
the associated source-topic subscription.

Create and manage destination feeds in Adafruit IO separately before enabling
their mappings. The firmware does not provision or delete feeds. Disabling or
renaming an output leaves existing Adafruit IO feeds and their history intact.
Changing mappings does not enable other source topics or add a provider.

[weatherkit_current.cpp](src/weatherkit_current.cpp) maps the 20 existing
`current*` destination feeds listed in the
[catalog](WEATHER_FEED_CATALOG.md#22-current-and-minute-mappings).
It preserves percent and whole-mile/mph truncation, Fahrenheit temperatures
with one decimal, compass labels, `Its light`/`Its dark`, and raw pressure
(hPa), precipitation intensity (mm/h), UV index, condition/trend, and `asOf`.
Wind input is km/h, not m/s. Shared precise conversion factors replace the old
approximate multipliers; exact unit boundaries can therefore differ from the
old implementation. Temperatures use the shared round-half-away-from-zero
helper rather than Python's tie-to-even rounding. Numeric conversions use
ArduinoJson's canonical decimal text in double precision to avoid float32
storage noise turning, for example, 65% humidity into a truncated 64%.

Missing, null, wrong-type, or out-of-range fields are skipped with serial
diagnostics, not replaced with artificial zeroes. Valid fields still publish;
an entirely unusable message is rejected. Valid zeroes are preserved. Optional
fields such as the altitude-specific cloud covers may be absent. Each push is
filtered against queued/acknowledged values; an entirely unchanged push creates
no publish or log-feed entry. A changed group completes with
`Sent feed group WeatherKit current - Number Updated: n` in `aggregator.log`.
Only hour 1 is enabled among hourly topics; daily topics are not subscribed yet.

MQTT-task delivery assembles messages up to 4096 bytes, then hands complete
snapshots to the main loop for parsing/publication. Across current, m5, m30, and hour1,
up to four received
snapshots wait behind one parsed snapshot; new arrivals beyond that limit are
discarded with a serial error. Invalid/incomplete chunk sequences and oversize
messages are likewise reported. If the publisher rejects a group, the parsed
snapshot remains for retries every two seconds, preserving FIFO order.
The source's `asOf` timestamp is published as a feed value, not used as an AIO
historical timestamp. Source-age rejection and heartbeat policy are deferred.

For the board check, upload the main environment, watch for
`WeatherKit current: parsed ... valid feed samples`, and verify
`currenttemperature`, `currenthumidity`, and the other available `current*`
feeds. A full initial group of 20 feeds needs roughly 40 seconds to drain,
plus queued logs. Subsequent unchanged pushes should show
`feed group WeatherKit current unchanged; nothing queued`.

### WeatherKit minute forecasts (m5 and m30)

The same MQTT connection subscribes to `forecast_minutes_5` and
`forecast_minutes_30` under the configured integration location, on every
connection. These are forecast horizons, not polling intervals. Both use the
shared receiver/parser in [weatherkit_current.cpp](src/weatherkit_current.cpp)
and the same FIFO change filter, two-second pacing, and bounded receive queue
as current conditions. Failed subscription enqueues retry independently every
15 seconds while connected.

| Source field | m5 destination | m30 destination | Output |
| --- | --- | --- | --- |
| `startTime` | `m5starttime` | `m30starttime` | Timestamp text |
| `precipitationChance` | `m5precipitationchance` | `m30precipitationchance` | Fraction to whole percent, truncated |
| `precipitationIntensity` | `m5precipitationintensity` | `m30precipitationintensity` | Raw nonnegative mm/h |

Missing/invalid fields are reported on serial and skipped; valid zero values
are published. Timestamp changes count as changes even when precipitation is
unchanged. Completely unchanged groups generate no feed writes or completion
logs. Completed changed groups produce local-time-prefixed entries such as:

```text
20261024120000 Sent feed group WeatherKit m5 - Number Updated: 3
20261024120006 Sent feed group WeatherKit m30 - Number Updated: 3
```

After uploading the main environment, wait for the next MQTT push and look for
`WeatherKit m5: parsed 3 valid feed samples` and the equivalent m30 message.
Verify the six existing minute feeds and both completion logs. A startup burst
of all 26 current/minute values needs roughly 52 seconds, plus logs, to drain;
arrival order determines which group publishes first. With hour 1 also enabled,
the startup burst is 47 values, roughly 94 seconds plus logs. Subsequent updates are
change-driven, not forced every five or thirty minutes. Do not run another
publisher against these same destination feeds.

### WeatherKit hour-1 forecast

The main MQTT connection also subscribes to `forecast_hours_1`. No other
hourly horizons are subscribed. Its 21 `hours1*` outputs follow the legacy
[hour-1 handler](circuitpython/Apple_Weatherkit/f1.py), with independently
enabled mappings in [feed_config.h](include/feed_config.h).

`forecastStart` is timestamp text; cloud cover, humidity, and precipitation
chance become whole percentages; temperatures become Fahrenheit with one
decimal; wind speeds become whole mph; wind direction becomes compass text.
Forecast daylight uses `It will be light` / `It will be dark`, unlike the
current-condition wording. Visibility becomes whole miles, matching current
conditions and correcting the legacy hour-1 handler's missing conversion.
Pressure (hPa), precipitation/snow amounts (mm), intensities (mm/h), UV,
condition, and trend retain their provider representations.

Missing/invalid fields are skipped and reported without manufactured zeroes.
All valid changed values use the shared FIFO publishing queue and rate limit.
After uploading, expect `WeatherKit hour1: parsed ... valid feed samples`,
updates to available `hours1*` feeds, and a log entry such as
`20261024123000 Sent feed group WeatherKit hour1 - Number Updated: 21`.
Some source fields may be absent, so the valid count can be lower than 21.
Unchanged pushes create no publishes or completion logs.

### Planned weather collection and publishing

The collection design is change-driven: fetch each polled provider response on a
request-level schedule, extract all of its feed samples, and publish only
samples whose values have changed. Most providers return multiple useful
measurements in one JSON response, so fetch frequency should be chosen for the
source data (for example, current weather every five minutes), not independently
for each Adafruit IO feed. A response can update many feeds while still using
one provider request.

The collection and publishing flow will be:

1. Fetch each provider request group at its configured interval. Requests that
   return distinct data, such as current/forecast data and historical data, can
   have different intervals.
2. Parse and validate the response, then use the provider adapter to produce
   feed samples with normalized values, formatting, and timestamps.
3. Compare each sample with the last successfully published value for that
   feed. Compare the final normalized/formatted value so insignificant
   floating-point differences do not cause unnecessary updates.
4. Queue changed samples and drain the queue at a controlled rate that respects
   the configured Adafruit IO publishing limit. Mark a sample as published
   only after the send succeeds; report failures through diagnostics.

The change-detection key for ordinary feeds is the feed. Historical feeds need
to include the sample timestamp (for example, the date) so updates to different
days are tracked independently. A future heartbeat can republish unchanged
values periodically so dashboards do not remain stale indefinitely. The
current implementation deliberately uses an in-memory cache and no heartbeat;
the freshness policy will be chosen alongside provider integration.

Fetch timing still matters for freshness, and queue pacing still matters when
many values change together (such as at startup or when a forecast updates).
The queue replaces per-feed timing rotations: a burst can be spread across
time without delaying the next provider fetch. Where a provider response is
large, parsing should retain only the fields needed by the configured feeds to
manage ESP32 memory use.

Open-Meteo is a useful example. The existing CircuitPython implementation
requests current/forecast data and, separately, past-day data; it then iterates
over days from that response to update dated historical feeds. These are two
request groups, not one request per feed or day. The ESP32 implementation can
schedule the past-data request less often than current conditions, and can
evaluate whether combining the requests is safe after measuring response size
and memory use.

Apple WeatherKit is planned as a pushed source rather than a polled one. The
Adafruit IO WeatherKit Power-Up (IO+) publishes each forecast block on its own
MQTT topic, `{username}/integration/weather/{location_id}/{type}`, for the types
`current`, `forecast_minutes_5`, `forecast_minutes_30`, `forecast_hours_1`,
`forecast_hours_2`, `forecast_hours_6`, `forecast_hours_12`, `forecast_hours_24`, `forecast_days_1`,
`forecast_days_2`, and `forecast_days_5`. These match the groups the
CircuitPython rotation (`f1.py`-`f8a.py`) split manually, and each message is
much smaller than the combined HTTP response from `receive_weather`. Each topic
acts as a request group whose updates arrive when Adafruit IO has new data;
samples still pass through the same change filter and publishing queue.
The integration API's `links.mqtt` response supplied on 2026-10-24 explicitly
advertises `forecast_hours_12`, despite its omission from the provider's
documentation topic list. Hour-12 message delivery has not yet been tested;
neither the main firmware nor the diagnostic currently subscribes to it.
Early measurements with the WeatherKit MQTT diagnostic build below show
non-retained pushes of its ten subscribed topics about once a minute; longer runs will confirm
cadence, change frequency, and stability before this is relied on.

## Isolated Wi-Fi diagnostic build

Use the `wifi_diagnostic` environment to test basic Wi-Fi independently of the
application. Its source filter compiles only
[wifi_diagnostic.cpp](src/wifi_diagnostic.cpp); the normal environment excludes
that file. The normal `seeed_xiao_esp32s3` environment remains the default.
No credential changes are required.

In PlatformIO Project Tasks, expand **wifi_diagnostic**, then choose **Upload
and Monitor** (or Upload followed by Monitor). Alternatively, in a PlatformIO
terminal:

```powershell
pio run -e wifi_diagnostic --target upload
pio device monitor -e wifi_diagnostic
```

Close any browser serial connection first. After boot, the program waits ten
seconds for the monitor, then runs five asynchronous scan-only tests without
connecting. Each reports result count, elapsed time, and scan completion events.
The diagnostic waits for the completion callback with a separate 20-second
deadline before reading results, bypassing the Arduino wrapper's six-second
polling timeout without changing its default per-channel dwell time.
An event status of -1 in the test summary means no completion arrived by the
deadline; status 0 denotes success.
Failed scans are cancelled and a two-second gap allows events to drain.
Send `2` to connect to credential #2, then `s` to repeat the five scans after
disconnecting that connection. This compares fresh-boot scanning against
post-connection scanning without radio restarts between scans. The scan batch
occupies the diagnostic loop; serial commands are processed after it completes.
It uses plain `WiFi.begin(ssid, password)` with no explicit channel/BSSID,
application scan, display/NeoPixel initialization, NTP, or application retry
policy during connection tests. Wi-Fi settings remain nonpersistent, and automatic reconnection is
disabled (the framework may still perform an internal initial retry).
The display is not updated and may retain old content until power is cycled;
pixels may likewise retain their previous color.

Each attempt times out after 30 seconds and then stops Wi-Fi. In the serial
monitor, send `1`-`9` to select a configured credential entry, `r` to retry the
selected entry, or `?` to repeat the help. A newline after commands is harmless.
The console reports disconnect reasons and, on success, IP address, signal
strength, and channel. It never prints passwords.

Verify connection to the phone hotspot, then test the original access points
individually. After success, leave the connection running to check stability.
This firmware is diagnostic-only, not a replacement for the application.
Restore the application using **seeed_xiao_esp32s3 / Upload and Monitor** or:

```powershell
pio run -e seeed_xiao_esp32s3 --target upload
```

## WeatherKit MQTT diagnostic build

The `weatherkit_mqtt_diagnostic` environment compiles only
[weatherkit_mqtt_diagnostic.cpp](src/weatherkit_mqtt_diagnostic.cpp). It is a
long-running observation tool for the planned WeatherKit MQTT source, with no
display or NeoPixel. It publishes compact summaries to `aggregator.diag`.
It requires `AIO_USERNAME`, `AIO_KEY`,
`AIO_WEATHER_LOCATION_ID` (the Power-Up location ID), and at least one Wi-Fi
network in `include/credentials.h`.

```powershell
pio run -e weatherkit_mqtt_diagnostic --target upload
pio device monitor -e weatherkit_mqtt_diagnostic
```

After a ten-second wait for the monitor, it cycles through saved Wi-Fi networks
in order (20 seconds each, no scan ranking), synchronizes NTP, then connects to
`mqtts://io.adafruit.com:8883`. TLS is verified against the DigiCert Global
Root G2 certificate (valid until 2038), so server-certificate renewals do not
require a firmware change; NTP time is required first for certificate validity
checks. It uses ESP-IDF's MQTT client, which reconnects automatically and
delivers messages larger than its 4 KB buffer in pieces, so payloads of any
size are measured rather than silently dropped.

On every connection, it subscribes to all ten WeatherKit topics plus
`{username}/errors` and `{username}/throttle`. Each received message logs local
time, uptime, forecast type, size, whether it is the first, changed, or
unchanged compared with that topic's previous payload (by hash), the retain
flag, time since the topic's previous message, time after subscribing for the
first message, and free heap. Changed messages also list the individual JSON
fields that changed, with old and new values (up to 15 per message). The full
JSON payload is printed for each topic's first message after boot. Up to 16 KB
of each topic's latest payload is kept for printing; the size and change
detection always cover the full message.

Messages are logged from the MQTT task into a queue (capped at 64 KB, with a
dropped-line count if exceeded) that `loop()` writes to the serial port.
Arduino-ESP32 2.0.x's native USB serial driver loses bytes when written from a
core other than the one that started it, which clipped log lines before this.

A summary table (message count, changes, retained count, last/maximum size,
average interval, and time since last message for each topic, plus MQTT
connection/error counts, RSSI, and heap) and the most frequently changed fields
for each topic print every 30 minutes. Serial
commands: `s` prints the summary now, `p` prints the latest payloads, `v`
toggles full payload logging for every message, `r` unsubscribes and
resubscribes to measure initial delivery again, and `?` repeats the help.
The Adafruit IO key and Wi-Fi passwords are never logged.

Each automatic 30-minute summary and manual `s` summary also queues one entry
to `aggregator.diag`, capped at 1024 bytes. Create this feed before running the
diagnostic. Entries include local time, uptime, MQTT connect/disconnect/error
counts, RSSI, current/minimum heap, and per-topic message/change/retained counts,
maximum payload bytes, average interval seconds, and seconds since last receipt.
Topic labels are `cur`, `min5`/`min30`, `hr1`/`hr2`/`hr6`/`hr24`, and
`day1`/`day2`/`day5`. Detailed field changes and full payloads remain serial-only.
Summary publishes use QoS 1 and are paced at least two seconds apart; serial
reports enqueue failures and MQTT acknowledgements. If disconnected, the latest
unsent snapshot waits for reconnection; a newer snapshot replaces it with an
explicit serial notice. RAM-only pending data is lost on reset, but entries
already delivered to Adafruit IO remain in the feed.

Questions to answer from a multi-hour run:

- Whether subscribing delivers data immediately (retained) or only at the next
  update, which decides whether startup needs an HTTP fetch.
- Message sizes, to choose parser buffers for the application.
- Update interval per topic, and how often each payload actually changes.
- Connection stability, and any Adafruit IO error or throttle messages.

Initial short runs showed that all ten topics are pushed together about once a
minute, not retained: the first data arrives at the next push, up to about a
minute after subscribing (one push was occasionally skipped). Sizes were about
91 bytes for `forecast_minutes_*`, 470-480 bytes for `forecast_hours_*`, 730
bytes for `current`, and 1.6 KB for `forecast_days_*`, with about 220 KB of
heap free. Many pushes repeat the previous payload unchanged.

Restore the application afterwards with the `seeed_xiao_esp32s3` upload above.

## NeoPixel ring

Connect D0 through a 330-470 ohm series resistor to the ring's DIN, preferably
placing the resistor near DIN. Connect ring power to USB-derived 5 V and share
ground with the XIAO. Keep data wiring short; a 3.3 V data signal into a 5 V ring
is not guaranteed for every pixel model. Add a suitable logic level shifter if
needed. Ensure the USB supply supports the board and ring together; the software
brightness setting is not an electrical current limiter.

The helper assumes eight standard 800 kHz GRB RGB pixels, not RGBW.
[status_pixels.h](include/status_pixels.h) exposes `begin()`,
`setPixel(index, red, green, blue)`, and `clear()`. Pixel indices are 0-7, and
color components are 0-255 before global brightness scaling. `setPixel` updates
only the selected pixel and sends the ring buffer immediately; `clear` turns
the whole ring off. Initialization and invalid-index errors are logged to USB
serial. The main loop owns pixels 0-2; pixels 3-7 are reserved for future indicators.
These calls should remain on the main loop task, not interrupt handlers.

| Position (index) | Purpose | Green | Amber | Red |
| --- | --- | --- | --- | --- |
| First (0) | Wi-Fi | Connected | Scanning/connecting | Offline/unconfigured |
| Second (1) | AIO publishing | Connected with a successful publish acknowledgement | Starting/reconnecting, no acknowledgement yet, acknowledgement pending for 10 seconds, publish enqueue failure, or account error/throttle | Unavailable for 30 seconds, or publish acknowledgement timeout |
| Third (2) | WeatherKit reception | Every enabled source topic has supplied usable data within 3 minutes | Waiting for first data, or any topic is at least 3 minutes old | MQTT disconnected after first connection, or any topic is at least 5 minutes old |

AIO and WeatherKit use the same MQTT connection. Pixel 1 reports transport and
publish acknowledgement health; pixel 2 tracks reception separately for current,
m5, m30, and hour1. Unchanged valid payloads refresh reception health. Malformed,
unusable, incomplete, or discarded messages do not. Reception is validated even
when the output queue is blocked, using the message's actual arrival time.
Groups with all mappings disabled are excluded. A disabled service's pixel is off.
Before the first MQTT connection, pixel 2 is amber for 30 seconds, then red;
its initial-data grace period is 5 minutes from the first connection.
Pixel 1's timeout/failure indication clears after a successful acknowledgement;
an idle queue does not become unhealthy simply because nothing changed.

The EYESPI BFF's optional IRQ line also uses D0; the current non-touch display
does not need it. D4/SDA and D5/SCL remain reserved for the STEMMA QT connector.
No I2C devices are initialized yet.

After uploading, check that pixel 0 progresses from amber to green, stays dim,
and changes to red following a Wi-Fi disconnection. Pixel 1 should become green
after the initial log acknowledgement, and pixel 2 after all enabled topics
arrive. Disconnect Wi-Fi to check pixels 0 and 2 turn red and pixel 1 progresses
from amber to red after 30 seconds (or earlier on an outstanding publish timeout).
Reconnect and verify publishing and source reception recover. Pixels 3-7 should
remain off. A successful build/initialization cannot verify the physical ring
or its RGB channel order.

## Shared weather transformations

[weather_transforms.h](include/weather_transforms.h) provides hardware-independent
C++11 helpers for Celsius to Fahrenheit, m/s and km/h to mph, meters to miles,
millimeters to inches, hPa to inHg, fractions to percent, seconds to hours,
16-point compass bearings, and decimal rounding.

Each helper returns a `TransformResult` with an explicit error; callers must
check `ok()` before using `value` and report failures through the provider's
diagnostics. Numeric failures carry NaN, not zero; compass failures carry a null
pointer. Missing JSON fields must be detected by the provider adapter before
conversion, never replaced with zero. Valid zero measurements remain valid.
Negative temperatures down to absolute zero are supported; negative speed,
distance, precipitation, duration, and nonpositive absolute pressure are rejected.
Fractions must be in [0, 1], and bearings in [0, 360]. Compass boundaries round
clockwise, with both 0 and 360 meaning north.

Conversions retain precision. `roundToDecimals` is a separate publication step
supporting 0-6 decimal places, rounding ties away from zero and normalizing
negative zero. It uses binary floating-point, not exact decimal arithmetic.
Providers must identify source units explicitly and avoid reconverting values
already in the desired units. A provider's missing-field policy, unit metadata,
output precision, and text formatting belong in its adapter.

These helpers are used by the WeatherKit current/minute adapter. Feed change detection
is implemented in the publisher. UTC/freshness handling, provider descriptions,
averaging, and pressure trends will
be added with the relevant collection/publishing infrastructure.

### Running transformation tests without hardware

[The standalone tests](test/test_weather_transforms.cpp) require a host C++11
compiler, not a board upload. On this development machine, the `ziglang` compiler
package is installed in PlatformIO's Python environment. From the project root
in PowerShell:

```powershell
New-Item -ItemType Directory -Force .pio\host-tests | Out-Null
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m ziglang c++ -std=c++11 -Wall -Wextra -Werror -pedantic -Iinclude test\test_weather_transforms.cpp -o .pio\host-tests\weather_transforms.exe
if ($LASTEXITCODE -eq 0) { & .\.pio\host-tests\weather_transforms.exe }
```

GCC or Clang can also compile this source with the same C++ flags. The host
compiler is development-only; no new firmware dependency is required.
The tests cover known conversions, valid zeroes, invalid values, overflow,
all compass sectors and boundaries, and rounding. They are a standalone host
executable, not a PlatformIO device test.

[Wi-Fi retry tests](test/test_wifi_retry.cpp) use the same host compilation
command with `test\test_wifi_retry.cpp` and a separate output executable.
They check the failure threshold, independent saved-network counters, successful
connection reset, exact cooldown boundary, timer wraparound, strongest-SSID
ranking, multiple satellite observations, and fallback when networks cool down.

[Diagnostic summary tests](test/test_diagnostic_summary.cpp) also use that host
command with a separate output executable. They check the compact field layout,
missing-data markers, all ten topics at maximum counter values within 1024
bytes, and explicit rejection of oversized output rather than publishing a
truncated summary.

[Publisher tests](test/test_aio_publisher.cpp) compile the production publisher
against a small fake MQTT/serial transport with dummy credentials, without
accessing the network or using real account credentials:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m ziglang c++ -std=c++11 -Wall -Wextra -Werror -pedantic -Itest\support -Iinclude "-I.pio\libdeps\seeed_xiao_esp32s3\ArduinoJson\src" test\test_aio_publisher.cpp -o .pio\host-tests\aio_publisher.exe
if ($LASTEXITCODE -eq 0) { & .\.pio\host-tests\aio_publisher.exe }
```

They check the two-second boundary, rolling 60-second windows shared by weather
and logs, group completion only after acknowledgements, unrelated message IDs,
reconnect ownership, timeout recovery and cleanup failures, enqueue failures,
atomic rejection of invalid/full groups, network/time startup gating, and
32-bit timer wraparound. These tests do not verify real broker delivery or TLS;
that still requires an on-board run.

Change-detection cases cover first values, duplicates before send and in flight,
acknowledged repeats, mixed changed/unchanged groups, FIFO reversions, repeated
keys within one group, formatting differences, retry state, cache capacity,
atomic rejection, and initial sends after a simulated reboot.

Build the main environment first to restore its declared ArduinoJson dependency.
[WeatherKit tests](test/test_weatherkit_current.cpp) use the publisher test
command with that test source and a separate output executable. They cover
all 20 feed mappings, valid zeroes/negative temperatures, missing/invalid fields,
malformed JSON, chunk assembly, oversize/overflow/disconnect handling, and
publish-queue retries across timer wraparound. Publisher tests additionally
exercise chunked MQTT events through the real adapter, change filter, paced
publishes, completion logging, and suppression of a repeated source snapshot.

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
   Set `AIO_USERNAME` and `AIO_KEY` for the main firmware's publisher and the
   WeatherKit MQTT diagnostic. WeatherKit current collection and the diagnostic
   also require
   `AIO_WEATHER_LOCATION_ID`. The provider keys are `OPENWEATHER_KEY`,
   `WEATHERAPI_KEY`,
   `VISUAL_CROSSING_KEY`, `WWO_KEY` (World Weather Online), and
   `EXCHANGE_RATE_KEY`. Historical settings names `openweather_token` and
   `WEATHER_API_KEY` duplicate `OPENWEATHER_KEY` and `WEATHERAPI_KEY`,
   respectively; use the canonical names in this header.
3. Run `pio run` or use PlatformIO's Build action.
4. Upload using PlatformIO's Upload action or `pio run --target upload`.
5. Open the serial monitor with `pio device monitor --baud 115200`.

The local credentials header is ignored by Git. Do not commit credentials.
Without it (or with all SSIDs empty), the firmware still builds and displays
configuration instructions without attempting to connect. Existing CircuitPython
settings are not read or copied automatically at runtime. Existing provider
credentials were migrated locally into the private header; the public template
contains placeholders only.

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
