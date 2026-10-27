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
forecasts, hour-1/hour-2/hour-6/hour-12/hour-24 forecasts, and day-1/day-2 forecasts
through Adafruit IO MQTT and publishes changed values through the shared paced
queue. Other providers and day-5 forecasts are not implemented yet.
[The feed catalog](WEATHER_FEED_CATALOG.csv) remains the migration reference.

### Adafruit IO publishing foundation

The application starts its TLS-verified MQTT connection after Wi-Fi and NTP are
ready. Feed samples are submitted in named groups; the queue publishes one
sample at a time, waits for its QoS 1 acknowledgement, and spaces publish
attempts by at least two seconds. This caps new application publishes at 30
per minute, including entries sent to `aggregator.log` and `aggregator.diag`.
Other publishers share the account quota, so this is not a guarantee against
account throttling. MQTT's QoS 1 retransmissions can repeat a publish while
waiting for an acknowledgement, including on an existing connection; they are
not new application queue entries and are outside this pacing.
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
serial. Throttle notices also pause new application publishes for 60 seconds
from the latest notice. All queued weather changes, logs, and diagnostics remain
queued, and matching ACKs still advance the queue during the pause. This does
not cancel an in-flight MQTT-owned message or its internal retransmissions.
After cooldown, normal two-second pacing resumes; further notices extend the
pause. The AIO pixel stays amber during cooldown unless a red condition applies.

### Account rate monitoring and publish tracing

The firmware reads [`GET /api/v2/{username}/throttle`](https://io.adafruit.com/api/docs/http.html#get-detailed-user-info)
using the existing account credentials in the `X-AIO-Key` header, verified HTTPS,
and no feed modifications. It samples once Wi-Fi/time are ready and then once
per minute. MQTT disconnects and throttle notices request an earlier check,
with at least 15 seconds between request starts and only one request running.
A temporary worker task keeps HTTP/TLS work outside the main loop; connection,
read, and TLS handshake timeouts limit individual blocking operations. Response
storage is capped at 512 bytes; oversized or malformed responses are failures.

The response's `active_data_rate` and `data_rate_limit` describe the entire
account, not just the aggregator. A sample at/above the limit, or HTTP 429,
also starts a 60-second pause. A lower sample never cancels an active cooldown.
The endpoint does not document a throttle-release countdown, so cooldown is
conservative rather than inferred from arbitrary notification text. Samples
cannot reserve capacity against other writers or prevent every throttle event.
Read requests use the separate HTTP-request allowance, not feed-data writes.
HTTP/JSON/task-start failures are reported explicitly on serial and counted;
the last successful sample retains its original timestamp rather than appearing
fresh or being replaced with a fabricated zero.

USB serial now prints one `AIO TX` line per successful application enqueue:
message ID, application timestamp in milliseconds, topic, byte count, and a
32-bit FNV-1a value fingerprint. Matching `AIO ACK` lines include message ID,
topic, and elapsed milliseconds; unmatched ACK IDs are reported separately.
Throttle notices include their first 191 payload bytes on serial. None of this
per-message tracing is forwarded to AIO, avoiding extra feed traffic.
These lines trace application enqueues, not the MQTT library's internal
retransmissions; dashboard records without additional TX lines narrow the
investigation but do not alone distinguish library retries from broker-side
duplication. No retransmission/reconnect timing settings are changed.

The 1 KB remote diagnostic adds:

```text
rate=25/60 age_s=12 fail=0 throttle=0 pause_s=0
```

This is active/limit, age of the last successful HTTP sample, failed rate checks,
cumulative MQTT throttle-notice count, and remaining application cooldown.
Before a successful sample, `rate=0/0 age_s=-` explicitly means unknown.
The existing MQTT `c/d/e` error count does not include broker throttle notices.

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

### Independent CircuitPython account monitor

[aio-status/code.py](aio-status/code.py) runs on a separate CircuitPython
Wi-Fi board with `board.NEOPIXEL`, not on the aggregator. Copy it to the board
as `code.py`. Keep the existing `settings.toml` entries `CIRCUITPY_WIFI_SSID`,
`CIRCUITPY_WIFI_PASSWORD`, `aio_username`, `aio_key`, and `TIMEZONE`.
Install matching CircuitPython bundle versions of `adafruit_io`,
`adafruit_requests`, `adafruit_minimqtt`, `adafruit_connection_manager`,
`adafruit_ticks`, and `neopixel`, including their bundle dependencies.
Create both destination feeds before running:

- `aggregator.aio-status`: unchanged five-column CSV value:
  `timestamp,remaining,average,samples,minimum`. Statistics cover successful
  HTTP observations in each approximately 30-second interval. Remaining
  capacity is limit minus active usage, not usage itself.
- `aggregator.aio-events`: timestamped JSON events and periodic rate snapshots.
  Event kinds include startup, connection/failure, broker error/throttle,
  failed rate checks/writes, missing samples, and replaced status reports.
  Rate snapshots include optional authorization/ban fields when returned by
  the API, local failure/drop counts, and free heap. Missing optional fields
  remain absent; no ban semantics are inferred from undocumented values.

One `get_user_rate_info()` call per approximately one-second sample supplies
both active and limit values. Full responses and full MQTT notification text
print to serial; remote event detail is truncated to 120 characters and
optional rate text to 40 characters. Raw MiniMQTT subscriptions to
`{username}/errors` and `{username}/throttle` capture text without the IO_MQTT
wrapper's exception-based error handling. These topics are not packet traces;
do not assume every error affecting a different client will reach this board.

Status reports are generated every 30 seconds; rate snapshots every 60 seconds.
Event-feed writes are limited to one per 30 seconds, with important events
ahead of replaceable rate snapshots. Both feeds share a minimum two-second
write spacing and 15-second attempt spacing: at most about four normal feed
writes per minute total. HTTP reads have a separate request allowance.
Callbacks only record events, never publish. Writes are deferred for 60 seconds
after a throttle notification, an HTTP throttle exception, exhausted sampled
capacity, or a failed write. Publishing also requires a successful sample no
older than five seconds and at least five remaining actions of headroom.
These checks cannot reserve capacity against other account writers.

The RAM event FIFO holds 16 entries; overflow prints explicitly and counts
dropped newest events. Only the latest unsent status and rate snapshots are
retained. Replaced status intervals are counted/reported; intervals with no
successful samples are reported as events instead of fabricated zero readings.
Failed writes retain their payload for retry, but an ambiguous HTTP failure
can still cause a duplicate on retry. A delayed report keeps its original
timestamp. Queues do not survive power loss, and monitoring cannot capture
broker events during a disconnect or blocking HTTP operation.

MQTT reconnect attempts are spaced by at least 15 seconds, with one library
connection attempt per call. Socket timeout uses MiniMQTT's one-second default,
with a matching one-second loop timeout and ten-second receive timeout. Socket
timeout also affects connection setup; shortening it to accelerate polling can
cause connection failures. Successful sample counts may be lower while MQTT
servicing or HTTP calls block; reports preserve the actual sample count.
Connection/loop exceptions print their type,
representation, and exception chain when the runtime exposes it. MiniMQTT's
info/warning/error messages also print the underlying connection failure before
it is wrapped; debug packet logging stays disabled. This does not require
`sys.print_exception`, which is unavailable on some CircuitPython runtimes.
Pending connection-failure events
are coalesced with a repeat count so retries cannot monopolize the event FIFO.
Wi-Fi reconnection is attempted on that path.
The existing LED thresholds are preserved. Periodic garbage collection remains,
but the old scheduled automatic reload is removed to avoid deliberate gaps.
Validate available sockets/heap and behavior on the actual monitor board;
host tests simulate the loop, not CircuitPython TLS or networking.

Run the focused host tests with `python -B -m unittest discover -s test -p test_aio_status.py`.

### Pico 2 W duplicate-delivery monitor

[aio-duplicates/code.py](aio-duplicates/code.py) is a separate CircuitPython
program for a second monitor board. It does not replace the independent
rate/error monitor or modify aggregator firmware. Install CircuitPython for
Pico 2 W and matching bundle libraries `adafruit_io`, `adafruit_requests`,
`adafruit_minimqtt`, `adafruit_connection_manager`, and `adafruit_ticks`,
including their bundle dependencies. The program uses built-in `hashlib.new("sha256", data)`
and does not require a NeoPixel or an SD card.
The Pico's built-in `board.LED` provides a single heartbeat flash every five
seconds and three flashes after each successful HTTP summary write (including
startup). Failed/deferred writes do not trigger the three-flash pattern.
The LED state machine adds no sleeps; existing blocking network operations can
stretch pulse timing. Heartbeat indicates main-loop activity, not a guarantee
that MQTT is connected.

Create `aggregator.aio-duplicates`, then copy the program to the Pico's
`CIRCUITPY` drive as `code.py`. Supply the same settings names used by the
rate monitor: `CIRCUITPY_WIFI_SSID`, `CIRCUITPY_WIFI_PASSWORD`, `aio_username`,
`aio_key`, and `TIMEZONE`. Do not commit credentials. Initial Wi-Fi/time setup
failures stop visibly; subsequent MQTT failures are reported and retried.

It subscribes at QoS 0 to `{username}/feeds/+`, plus `/errors` and `/throttle`.
Using `+` excludes `/json` and `/csv` variants that could otherwise be mistaken
for duplicates. The monitor's own output topic is excluded. Broker events print
on serial; throttle notices defer summary writes. No subscription changes are
made to other devices.

Each feed is tracked independently by payload byte length and SHA-256 digest,
with a 48-byte preview (decoded with replacement for incomplete/invalid UTF-8).
Full payloads are not retained; digest matches are a practical identity check,
not a mathematical guarantee of identical content. The default burst threshold
is three total matching deliveries, no more than three seconds apart, within
a window of at most ten seconds. A value change, idle gap, elapsed window,
capacity eviction, or disconnect finalizes a burst. Continuous storms split
into bounded windows. Two copies alone are not reported as a burst; individual
repeats still print on serial. Threshold constants are at the top of the file.

Serial output includes `REPEAT` lines, finalized JSON `REPORT` lines, networking
errors, and a once-per-minute `HEALTH` line with reception, burst, capacity,
queue/drop, failure and heap counts. `TRACE_ALL = True` additionally logs every
base-feed message to serial (potentially verbose, with full payload contents).
Capture serial externally if durable complete evidence is needed.

Finalized reports contain the feed key, first/last local receipt timestamps,
copy count, elapsed receipt time, payload length, full digest, preview, and
termination reason. `span_s` uses monotonic time; it is not broker creation
time. A startup report verifies output connectivity even before any bursts occur.
The FIFO holds 16 pending reports and drops newest on overflow with an explicit
serial message/count. Tracking is limited to 640 currently active feeds; capacity
evictions are explicit and counted. Expired baselines are removed rather than
retaining all feed payloads indefinitely.

Summary attempts are separated by at least 30 seconds (at most two writes per
minute). Before each attempt, one HTTP rate check must show at least five
remaining account actions. Low headroom, throttle notices, or failed writes
defer sending for 60 seconds. Callbacks never publish. Failed writes keep the
report for retry; ambiguous HTTP failures can still duplicate a summary.
All reports print locally before entering the bounded FIFO, so they remain
observable over serial even if remote delivery is deferred or dropped.

This records **observed broker deliveries**, not originating publisher identity
or outbound MQTT retransmission flags. QoS 0 avoids subscriber-side QoS 1 retries,
but permits lost observations. MQTT buffering during HTTP requests or scheduler
delays can make delivered messages appear closer together than their original
publish times. Reconnects clear baselines and print capture gaps; RAM evidence
does not survive reset. It is a burst detector, not a durable all-feed recorder
or proof that every identical value was an erroneous publish. Compare reports
with feed exports, aggregator TX/ACK traces, and the independent capacity monitor.

Host tests simulate interleaved bursts, normal periodic readings, reversions,
payload bounds, queue/connection behavior and sending policy:
`python -B -m unittest discover -s test -p test_aio_duplicates.py`.
Verify TLS, dependency compatibility and actual free heap on the Pico before
leaving it unattended.

### Main-firmware soak-test diagnostics

Create `aggregator.diag` externally in Adafruit IO. The main firmware sends a
compact snapshot there after its first MQTT connection, every 30 minutes, and
after recovery from an MQTT outage. Recovery snapshots have a 60-second
cooldown since the last snapshot; rapid reconnects are counted and combined
into a deferred recovery snapshot while connected. Existing feed history is
not cleared. The isolated WeatherKit diagnostic has its own summary format.

Each snapshot is at most 1024 bytes and begins with local capture time, a
`startup` / `periodic` / `recovery` label, uptime in seconds (`up_s`), and the
ESP-IDF numeric reset reason (`reset`: commonly 1=power-on, 3=software,
4=panic, 5=interrupt watchdog, 6=task watchdog, 7=other watchdog, 9=brownout).
It includes:

- `mqtt`, `c/d/e`: connection state and cumulative connections, losses of an
  established connection, and MQTT error events since boot. Callback counters
  include reconnects that occur between main-loop updates.
- `outage_s current/last/max`: current outage age, last completed outage, and
  longest outage, including an ongoing outage. Initial connection waiting is
  not an outage.
- `ack`, `timeout`, `enq_fail`, `group_defer`: matching publish acknowledgements
  (weather, operational logs, and diagnostics), acknowledgement-timeout/reset
  attempts, failed MQTT publish enqueues, and group admission deferrals due to
  queue/cache capacity (previously `group_reject`). Deferred groups are retained
  and retried, so a nonzero count is not data loss; true discards are reported
  by `wk_drop`. A retained group's repeated attempts each count.
- `queue`, `log_wait`, `high`: current shared publish queue size (including the
  in-flight head), operational logs waiting for promotion, and high-water mark
  of their combined size. The publish queue is bounded at 64 and the separate
  operational-log waiting queue at 16.
- `log_drop`, `diag_coalesced`: operational logs dropped when their waiting
  queue is full, and obsolete unsent diagnostic snapshots replaced by newer
  ones.
- `inflight_id`, `age_s`: current MQTT publish ID and time awaiting its
  acknowledgement.
- `last_error`: whether SDK details were supplied, MQTT error type, ESP/TLS
  error codes, certificate flags, socket errno, broker refusal code, and age
  in seconds. Only fields relevant to the last error type are populated.
  `c/d/e`'s error count of zero means no MQTT error has been observed.
- `wk_drop`: cumulative discarded WeatherKit messages. The adjacent
  `assembly/raw/invalid/change` counters distinguish oversize/invalid/incomplete
  assembly, raw inbox overflow, malformed/wholly unusable payloads, and changed
  snapshots rejected at backlog capacity. A partially valid payload is usable.
  Unchanged snapshots filtered before backlog admission are not discards.
- Per-source `age_s`: time since the last usable payload, including unchanged
  payloads; `-` means no usable data yet, `off` means the source or all of its
  output mappings are disabled.
- Wi-Fi state, RSSI in dBm, free heap, and minimum free heap in bytes.

Snapshots share the existing two-second publish pacing and QoS 1 acknowledgement
path; they do not generate feed-group completion logs or enter the change cache.
One diagnostic can occupy the publish queue, plus one latest snapshot waiting
outside it. New snapshots replace older unsent diagnostics, never an in-flight
message or queued weather changes. Consequently a long outage cannot create
an unbounded diagnostic backlog. Snapshot time, counters, and ages describe
capture, not eventual delivery; the snapshot's own acknowledgement appears in
a later snapshot.

MQTT errors now include available TLS/socket or broker-refusal details in
`aggregator.log` and serial, emitted from the main loop rather than the MQTT
callback. Several errors before one loop update are counted individually, but
only the most recent error details are logged. Publish-timeout logs also include
the message ID and topic.

All counters and queues remain RAM-only and reset on reboot. Summaries improve
visibility after a recoverable outage, but cannot preserve unsent information
through power loss. For board validation, check one startup summary arrives,
compare its timestamp/uptime and source ages with serial, then verify a recovery
snapshot after a reconnect and a periodic snapshot after 30 minutes. Diagnostic
publishes must remain at least two seconds apart from other new publishes.

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
configures 21 enabled hour-1 outputs; `weatherkitHour2`, `weatherkitHour6`, and
`weatherkitHour12` configure 21 outputs each for `hours2*`, `hours6*`, and `hours12*`.
`weatherkitHour24` configures the corresponding 21 `hours24*` outputs.
`weatherkitDay1` configures 56 outputs across whole-day, daytime, and overnight.
`weatherkitDay2` independently configures the corresponding 56 `days2*` outputs.
Nested source fields use `object.field` paths, for example
`daytimeForecast.temperatureMax`; ordinary root field names are unchanged.

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
Hours 1, 2, 6, 12, and 24 and days 1, 2, and 5 are enabled.

MQTT-task delivery assembles messages up to 4096 bytes into a ten-message
raw inbox. Every main-loop update drains that inbox, even when publishing is
blocked. Parsing, validation, and serial reporting run outside the MQTT receive
mutex. Usable messages refresh per-topic health using their arrival time.

Before backlog admission, normalized values are compared with the last values
accepted from that source. Unchanged fields need no backlog space; a wholly
unchanged message refreshes health without adding an entry. This prevents
repeated identical minute pushes from filling the backlog during an outage.
Missing/invalid fields do not erase earlier accepted values.

Genuine changed snapshots wait in a separate ten-entry FIFO behind one
snapshot retained for two-second publish retries. Reversions such as A/B/A and
cross-group arrival order are preserved; newer snapshots never overwrite
accepted changes. The existing publisher still performs its final comparison
against queued/acknowledged values. Both RAM caches reset on reboot.

Storage remains bounded, not lossless for unlimited changing data: raw inbox
overflow or a full changed-snapshot backlog discards the newest snapshot and
reports its specific cause. A rejected change does not advance the source's
accepted-value cache, allowing a later push to retry it. Its usable reception
still refreshes health, which measures source availability rather than delivery.
Malformed/incomplete messages remain explicit errors. Separate cumulative drop
counters are included in `aggregator.diag`.
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
arrival order determines which group publishes first. With hours 1, 2, 6, 12, and 24
and days 1 and 2 also enabled, the startup burst is 243 values, roughly 486 seconds plus logs. This exceeds the
64-entry publish queue; the adapter retains and retries groups as space opens,
without increasing the publish rate. Subsequent updates are
change-driven, not forced every five or thirty minutes. Do not run another
publisher against these same destination feeds.

### WeatherKit hour-1 forecast

The main MQTT connection subscribes to `forecast_hours_1`, `forecast_hours_2`,
`forecast_hours_6`, `forecast_hours_12`, and `forecast_hours_24`.
The hour-1 topic's 21 `hours1*` outputs follow the legacy
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

### WeatherKit hour-2 forecast

`forecast_hours_2` uses 21 independently configurable mappings in
`weatherkitHour2`, following the legacy [hour-2 handler](circuitpython/Apple_Weatherkit/f2.py).
Fields, conversions, validation, FIFO retries, and change detection match hour 1.
Visibility is whole miles, correcting the legacy handler's raw-meter output.
Days 1 and 2 are enabled; day 5 remains disabled.

After uploading, expect `WeatherKit hour2: parsed 21 valid feed samples` when all
fields are present, updates to the existing `hours2*` feeds, and
`Sent feed group WeatherKit hour2 - Number Updated: 21` in `aggregator.log`.
Subsequent unchanged messages produce no updates; changed messages log only
their changed count. The WeatherKit pixel now waits for usable hour-2 data too,
and `aggregator.diag` includes its age as `forecast_hours_2`.

### WeatherKit hour-6 forecast

`forecast_hours_6` uses 21 independently configurable mappings in
`weatherkitHour6`, following the legacy [hour-6 handler](circuitpython/Apple_Weatherkit/f3.py).
Conversions, validation, FIFO retries, and change detection match hours 1 and 2,
including visibility in whole miles rather than the legacy raw meters.

After uploading, expect `WeatherKit hour6: parsed 21 valid feed samples` when all
fields are present, updates to existing `hours6*` feeds, and
`Sent feed group WeatherKit hour6 - Number Updated: 21` in `aggregator.log`.
The WeatherKit pixel includes hour-6 freshness, and `aggregator.diag` reports
its age as `forecast_hours_6`. Startup queue-full rejections retain the parsed
group for retry; they do not discard it.

### WeatherKit hour-12 forecast

`forecast_hours_12` uses 21 independently configurable mappings in
`weatherkitHour12`, following the legacy [hour-12 handler](circuitpython/Apple_Weatherkit/f4.py).
Conversions and validation match the other hourly horizons, including whole-mile
visibility, Fahrenheit temperatures, whole-mph wind speeds, and forecast daylight
wording. Changed values share the same paced FIFO, retry, and completion-log path.

The API advertises this MQTT topic despite its omission from the provider's
documentation; live delivery was confirmed on hardware on 2026-10-24. After uploading, look
for `WeatherKit hour12: parsed 21 valid feed samples`, updates to `hours12*`, and
`Sent feed group WeatherKit hour12 - Number Updated: 21`.
Hour-12 freshness participates in the WeatherKit pixel and appears in
`aggregator.diag` as `forecast_hours_12`. If the topic does not supply usable
data, the pixel remains amber and becomes red after the normal five-minute
initial-data grace period.

### WeatherKit hour-24 forecast

`forecast_hours_24` uses 21 independently configurable mappings in
`weatherkitHour24`, following the legacy [hour-24 handler](circuitpython/Apple_Weatherkit/f5.py).
Conversions and validation match the other hourly horizons, including visibility
in whole miles, Fahrenheit temperatures, whole-mph wind speeds, and the
provider-supplied `pressureTrend`. No historical pressure-trend calculation is used.

After uploading, expect `WeatherKit hour24: parsed 21 valid feed samples` when
all fields are present, updates to existing `hours24*` feeds, and
`Sent feed group WeatherKit hour24 - Number Updated: 21` in `aggregator.log`.
Hour-24 freshness participates in the WeatherKit pixel and appears in
`aggregator.diag` as `forecast_hours_24`. Unchanged pushes refresh freshness
without consuming change-backlog entries. Publish queue capacity rejections
retain groups for retry.

### WeatherKit day-1 forecast

`forecast_days_1` uses 56 independently configurable mappings in
`weatherkitDay1`, covering the legacy [overnight handler](circuitpython/Apple_Weatherkit/f6.py)
and [whole-day/daytime handler](circuitpython/Apple_Weatherkit/f6a.py):

- 26 root fields publish to `days1*`.
- 15 `daytimeForecast` fields publish to `days1daytime*`.
- 15 `overnightForecast` fields publish to `days1ovnt*`.

Temperatures become Fahrenheit with one decimal, wind speeds whole mph,
wind direction compass text, and fractional cloud/humidity/precipitation chances
whole percentages. Precipitation/snow amounts remain mm. UV index remains raw;
moon phase and celestial/forecast timestamps remain provider text. Missing/null
moonrise or moonset may be legitimate and are skipped, never fabricated as `"0"`.
Absent or invalid nested objects do not prevent valid root/other-period values
from publishing. Day 2 is independently enabled; day 5 remains disabled.
Daily adapters are not chained together.

The entire day-1 payload is one source group, not three independent MQTT topics.
Its up to 56 changed outputs fit atomically in the existing 64-entry publish
queue; the adapter retains the group until enough space opens. It shares the
same FIFO, change filter, two-second pacing, and counted completion log.
Usable day-1 data participates in the WeatherKit pixel and appears as
`forecast_days_1` in `aggregator.diag`.

After uploading, look for `WeatherKit day1: parsed 56 valid feed samples`
(fewer if provider fields are absent), updates to the three day-1 feed sets, and
`Sent feed group WeatherKit day1 - Number Updated: 56` on a complete initial
payload. Repeated unchanged pushes do not consume backlog space. The complete
243-output startup burst across ten groups takes about nine minutes plus
source arrival time and any connection delays. Publish queue-full retries are
expected while the large group waits; receive-discard counters should remain zero.

### WeatherKit day-2 forecast

`forecast_days_2` uses 56 independently configurable mappings in
`weatherkitDay2`, matching the legacy [overnight](circuitpython/Apple_Weatherkit/f7.py)
and [whole-day/daytime](circuitpython/Apple_Weatherkit/f7a.py) handlers:
26 `days2*`, 15 `days2daytime*`, and 15 `days2ovnt*` outputs.
Nested paths, conversions, missing-field handling, atomic queue admission,
FIFO retries, and change filtering match day 1. Each topic maintains its own
accepted-value cache and source freshness, including the WeatherKit pixel and
`forecast_days_2` age in diagnostics. Day 5 (`weatherkitDay5`, 56 `days5*`
outputs, same layout) is subscribed as `forecast_days_5`; its 56 destination
feeds must already exist. The tracked-feed cap is 384 and the snapshot backlog 16.

After uploading, expect `WeatherKit day2: parsed 56 valid feed samples`
(fewer for absent provider fields) and
`Sent feed group WeatherKit day2 - Number Updated: 56` on a complete initial
payload. Subsequent completion counts include changed values only. Feed
creation remains external; all 56 existing destination feeds must be available.
Allow about nine minutes plus source-arrival time, throttling, and connection
delays for the complete startup across ten groups. Both daily groups may wait
for near-empty queue capacity; queue-full retained retries are expected.
The shared two-second pacing and account-rate cooldown policy are unchanged.
Watch remaining account headroom and receive-discard counters during this
controlled load increase; the application cap does not reserve account quota.

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
documentation topic list. The main firmware now subscribes to it; live hour-12
message delivery was confirmed on hardware on 2026-10-24. The isolated diagnostic remains
unchanged and does not subscribe to hour 12.
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
m5, m30, hour1, hour2, hour6, hour12, hour24, day1, and day2. Unchanged valid payloads refresh reception health. Malformed,
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
and averaging will be added with the relevant collection/publishing infrastructure.
Calculating barometric trends from historical pressure readings is out of scope:
WeatherKit supplies `pressureTrend` for current conditions and hourly forecasts,
and those provider-supplied values are passed through to the configured feeds.

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
