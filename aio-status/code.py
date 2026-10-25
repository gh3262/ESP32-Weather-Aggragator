import os
import gc
import json
import time
import rtc
import ssl
import wifi
import neopixel
import socketpool
import board

import adafruit_requests
from adafruit_io.adafruit_io import IO_HTTP
from adafruit_io.adafruit_io_errors import AdafruitIO_RequestError, AdafruitIO_ThrottleError
from adafruit_minimqtt.adafruit_minimqtt import MQTT, MMQTTException


STATUS_FEED = "aggregator.aio-status"
EVENT_FEED = "aggregator.aio-events"
POLL_SECONDS = 1
REPORT_SECONDS = 30
RATE_EVENT_SECONDS = 60
EVENT_SEND_SECONDS = 30
RETRY_SECONDS = 15
THROTTLE_SECONDS = 60
MAX_EVENTS = 16
MQTT_SOCKET_TIMEOUT = 1

RED = 0xFF0000
GREEN = 0x00FF00
BLUE = 0x0000FF
YELLOW = 0xFFFF00
WHITE = 0xFFFFFF


class MqttDiagnosticLogger:
    def debug(self, message, *args):
        pass

    def info(self, message, *args):
        print("MQTT LIB:", message % args if args else message)

    warning = info
    error = info
    critical = info


def print_mqtt_exception(exc):
    print("MQTT exception:", type(exc).__name__, repr(exc))
    for _ in range(4):
        cause = getattr(exc, "__cause__", None)
        if cause is None:
            cause = getattr(exc, "__context__", None)
        if cause is None or cause is exc:
            break
        print("MQTT caused by:", type(cause).__name__, repr(cause))
        exc = cause


def timestamp_string():
    now = rtc.RTC().datetime
    return "{:04d}{:02d}{:02d}{:02d}{:02d}{:02d}".format(
        now.tm_year, now.tm_mon, now.tm_mday,
        now.tm_hour, now.tm_min, now.tm_sec
    )


def queue_event(kind, detail):
    global event_drops
    event = {
        "timestamp": timestamp_string(),
        "kind": kind,
        "detail": str(detail)[:120],
    }
    print("EVENT", event["timestamp"], kind, detail)
    if kind == "mqtt_connect_failed":
        for queued in events:
            if queued["kind"] == kind:
                queued["timestamp"] = event["timestamp"]
                queued["detail"] = event["detail"]
                queued["repeats"] = queued.get("repeats", 1) + 1
                return
    if len(events) >= MAX_EVENTS:
        event_drops += 1
        print("EVENT QUEUE FULL; newest event dropped; total=", event_drops)
    else:
        events.append(event)


def on_message(client, topic, message):
    global pause_until, throttle_count, error_count
    if topic == aio_username + "/throttle":
        throttle_count += 1
        pause_until = time.monotonic() + THROTTLE_SECONDS
        queue_event("throttle", message)
    elif topic == aio_username + "/errors":
        error_count += 1
        queue_event("broker_error", message)


def on_connect(client, userdata, flags, result):
    client.subscribe(aio_username + "/throttle")
    client.subscribe(aio_username + "/errors")
    queue_event("mqtt_connected", "error/throttle subscriptions requested")


def read_remaining(info):
    if not isinstance(info, dict):
        raise ValueError("Rate response is not an object")
    limit = info.get("data_rate_limit")
    active = info.get("active_data_rate")
    if (type(limit) is not int or type(active) is not int
            or limit <= 0 or active < 0):
        raise ValueError("Invalid data_rate_limit or active_data_rate")
    return limit - active


def make_rate_event(info):
    # Optional fields are provider observations, not local control inputs.
    payload = {
        "timestamp": timestamp_string(),
        "kind": "rate",
        "rates": {
            key: value if type(value) in (int, float, bool, type(None))
            else str(value)[:40]
            for key, value in info.items()
            if key in (
                "data_rate_limit", "active_data_rate", "authentication_rate",
                "subscribe_authorization_rate", "publish_authorization_rate",
                "hourly_ban_rate", "mqtt_ban_error_message", "active_sms_rate"
            )
        },
        "poll_fail": poll_failures,
        "write_fail": write_failures,
        "mqtt_fail": mqtt_failures,
        "throttle": throttle_count,
        "errors": error_count,
        "event_drop": event_drops,
        "status_drop": status_drops,
        "heap": gc.mem_free(),
    }
    return json.dumps(payload)


bp = neopixel.NeoPixel(board.NEOPIXEL, 1, brightness=0.5)
for color in (RED, GREEN, BLUE):
    bp[0] = color
    time.sleep(1)

aio_username = os.getenv("aio_username")
aio_key = os.getenv("aio_key")
if not aio_username or not aio_key:
    raise ValueError("settings.toml must define aio_username and aio_key")

print("Connecting to WiFi...")
wifi.radio.connect(
    os.getenv("CIRCUITPY_WIFI_SSID"), os.getenv("CIRCUITPY_WIFI_PASSWORD")
)
pool = socketpool.SocketPool(wifi.radio)
requests = adafruit_requests.Session(pool, ssl.create_default_context())
io = IO_HTTP(aio_username, aio_key, requests)
rtc.RTC().datetime = time.struct_time(io.receive_time(os.getenv("TIMEZONE")))

mqtt = MQTT(
    broker="io.adafruit.com", port=8883,
    username=aio_username, password=aio_key, is_ssl=True,
    socket_pool=pool, ssl_context=ssl.create_default_context(),
    socket_timeout=MQTT_SOCKET_TIMEOUT, recv_timeout=10, connect_retries=1,
)
mqtt.on_connect = on_connect
mqtt.on_message = on_message
mqtt.logger = MqttDiagnosticLogger()

events = []
event_drops = status_drops = 0
poll_failures = write_failures = mqtt_failures = 0
throttle_count = error_count = 0
pause_until = 0
rrl = None
sample_sum = sample_count = 0
sample_min = None
last_sample = -100
last_poll = last_report = time.monotonic()
last_rate_event = last_event_send = last_report - RATE_EVENT_SECONDS
last_write = last_write_attempt = last_mqtt_attempt = -100
last_gc = time.monotonic()
pending_status = pending_rate = None
mqtt_ready = False

print("Monitoring started; status columns: timestamp,remaining,average,samples,minimum")
queue_event("startup", "independent HTTP/MQTT monitor")

while True:
    now = time.monotonic()
    if not mqtt_ready and now - last_mqtt_attempt >= RETRY_SECONDS:
        last_mqtt_attempt = now
        try:
            if not wifi.radio.connected:
                wifi.radio.connect(
                    os.getenv("CIRCUITPY_WIFI_SSID"),
                    os.getenv("CIRCUITPY_WIFI_PASSWORD"),
                )
            mqtt.connect()
            mqtt_ready = True
        except (MMQTTException, OSError, RuntimeError, ValueError) as exc:
            mqtt_failures += 1
            print_mqtt_exception(exc)
            queue_event("mqtt_connect_failed", exc)
            if mqtt.is_connected():
                try:
                    mqtt.disconnect()
                except (MMQTTException, OSError, RuntimeError) as close_error:
                    print("MQTT cleanup:", close_error)

    if mqtt_ready:
        try:
            mqtt.loop(timeout=MQTT_SOCKET_TIMEOUT)
        except (MMQTTException, OSError, RuntimeError) as exc:
            mqtt_failures += 1
            print_mqtt_exception(exc)
            mqtt_ready = False
            last_mqtt_attempt = time.monotonic()
            queue_event("mqtt_loop_failed", exc)
            if mqtt.is_connected():
                try:
                    mqtt.disconnect()
                except (MMQTTException, OSError, RuntimeError) as close_error:
                    print("MQTT cleanup:", close_error)

    now = time.monotonic()
    if now - last_poll >= POLL_SECONDS:
        last_poll = now
        try:
            info = io.get_user_rate_info()
            remaining = read_remaining(info)
        except (AdafruitIO_RequestError, AdafruitIO_ThrottleError,
                OSError, RuntimeError, ValueError) as exc:
            poll_failures += 1
            bp[0] = RED
            queue_event("rate_check_failed", exc)
            if isinstance(exc, AdafruitIO_ThrottleError):
                pause_until = time.monotonic() + THROTTLE_SECONDS
        else:
            last_sample = time.monotonic()
            rrl = remaining
            sample_sum += rrl
            sample_count += 1
            sample_min = rrl if sample_min is None else min(sample_min, rrl)
            print("RATE", timestamp_string(), json.dumps(info))
            # Preserve the original monitor's LED thresholds.
            bp[0] = (GREEN if rrl < 15 else YELLOW if rrl < 25
                     else BLUE if rrl < 35 else RED if rrl < 45 else WHITE)
            if rrl <= 0:
                pause_until = max(pause_until, last_sample + THROTTLE_SECONDS)
            if last_sample - last_rate_event >= RATE_EVENT_SECONDS:
                pending_rate = make_rate_event(info)
                last_rate_event = last_sample

    now = time.monotonic()
    if now - last_report >= REPORT_SECONDS:
        if sample_count:
            msg = "{},{},{:.1f},{},{}".format(
                timestamp_string(), rrl, sample_sum / sample_count,
                sample_count, sample_min
            )
            print("REPORT", msg)
            if pending_status is not None:
                status_drops += 1
                queue_event("status_replaced", "unsent status replaced by latest interval")
            pending_status = msg
        else:
            queue_event("no_samples", "no successful rate samples this interval")
        sample_sum = sample_count = 0
        sample_min = None
        last_report = now

    # Never publish from callbacks or let event traffic create a throttle loop.
    if (now >= pause_until and rrl is not None and rrl >= 5
            and now - last_sample <= 5 and now - last_write >= 2
            and now - last_write_attempt >= RETRY_SECONDS):
        feed = payload = None
        event_kind = None
        if pending_status is not None:
            feed, payload = STATUS_FEED, pending_status
        elif now - last_event_send >= EVENT_SEND_SECONDS:
            if events:
                feed, payload = EVENT_FEED, json.dumps(events[0])
                event_kind = "event"
            elif pending_rate is not None:
                feed, payload = EVENT_FEED, pending_rate
                event_kind = "rate"
        if payload is not None:
            last_write_attempt = now
            if len(payload.encode("utf-8")) > 1024:
                raise ValueError("Monitor payload exceeds 1024 bytes")
            try:
                io.send_data(feed, payload)
            except (AdafruitIO_RequestError, AdafruitIO_ThrottleError,
                    OSError, RuntimeError, ValueError) as exc:
                write_failures += 1
                queue_event("feed_write_failed", exc)
                # Delivery may be ambiguous; retain for retry, never claim success.
                pause_until = time.monotonic() + THROTTLE_SECONDS
            else:
                last_write = time.monotonic()
                print("SENT", feed, payload)
                if feed == STATUS_FEED:
                    pending_status = None
                else:
                    last_event_send = last_write
                    if event_kind == "event":
                        events.pop(0)
                    else:
                        pending_rate = None

    if now - last_gc >= 60:
        gc.collect()
        print("Free memory:", gc.mem_free(), "queued events:", len(events))
        last_gc = now
    time.sleep(0.05)
