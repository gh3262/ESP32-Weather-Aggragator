"""Passive, bounded Adafruit IO duplicate-delivery monitor for Pico 2 W."""
import os
import gc
import json
import time
import rtc
import ssl
import hashlib
import wifi
import socketpool
import board
import digitalio

import adafruit_requests
from adafruit_io.adafruit_io import IO_HTTP
from adafruit_io.adafruit_io_errors import AdafruitIO_RequestError, AdafruitIO_ThrottleError
from adafruit_minimqtt.adafruit_minimqtt import MQTT, MMQTTException


OUTPUT_FEED = "aggregator.aio-duplicates"
MAX_FEEDS = 640
MAX_REPORTS = 16
WINDOW_SECONDS = 10
QUIET_SECONDS = 3
MIN_COPIES = 3
SEND_SECONDS = 30
RETRY_SECONDS = 15
TRACE_ALL = False


class StatusLed:
    def __init__(self, pin, now):
        self.pin = pin
        self.pin.value = False
        self.last_heartbeat = now
        self.next_edge = now
        self.edges_left = 0

    def sent(self, now):
        self.pin.value = True
        self.edges_left = 5
        self.next_edge = now + 0.2
        self.last_heartbeat = now

    def update(self, now):
        if self.edges_left:
            if now >= self.next_edge:
                self.pin.value = not self.pin.value
                self.edges_left -= 1
                self.next_edge = now + 0.2
            return
        if now - self.last_heartbeat >= 5:
            self.pin.value = True
            self.edges_left = 1
            self.next_edge = now + 0.2
            self.last_heartbeat = now


def timestamp_string():
    now = rtc.RTC().datetime
    return "{:04d}{:02d}{:02d}{:02d}{:02d}{:02d}".format(
        now.tm_year, now.tm_mon, now.tm_mday,
        now.tm_hour, now.tm_min, now.tm_sec
    )


class DuplicateMonitor:
    def __init__(self, username):
        self.prefix = username + "/feeds/"
        self.excluded = self.prefix + OUTPUT_FEED
        self.feeds = {}
        self.reports = []
        self.messages = self.bursts = self.report_drops = self.evictions = 0
        self.invalid_topics = 0

    def queue(self, report):
        print("REPORT", json.dumps(report))
        if len(self.reports) >= MAX_REPORTS:
            self.report_drops += 1
            print("REPORT QUEUE FULL; newest dropped:", self.report_drops)
        else:
            self.reports.append(report)

    def finish(self, topic, state, reason):
        if state["copies"] >= MIN_COPIES:
            self.bursts += 1
            self.queue({
                "kind": "duplicate_delivery_burst", "feed": topic[len(self.prefix):],
                "first": state["first"], "last": state["last"],
                "copies": state["copies"],
                "span_s": round(state["last_at"] - state["start_at"], 3),
                "bytes": state["bytes"], "sha256": state["hash"].hex(),
                "preview": state["preview"], "end": reason,
            })

    def receive(self, topic, message, now, stamp):
        if topic == self.excluded or not topic.startswith(self.prefix):
            return
        key = topic[len(self.prefix):]
        if not key or "/" in key or len(key) > 128:
            self.invalid_topics += 1
            print("IGNORED invalid/non-base feed topic:", topic)
            return
        self.messages += 1
        value = message.encode("utf-8") if isinstance(message, str) else bytes(message)
        digest = hashlib.new("sha256", value).digest()
        if TRACE_ALL:
            print("RX", stamp, topic, len(value), digest.hex(), repr(message))
        previous = self.feeds.get(topic)
        if previous is not None:
            same = previous["hash"] == digest and previous["bytes"] == len(value)
            if (same and now - previous["last_at"] <= QUIET_SECONDS
                    and now - previous["start_at"] <= WINDOW_SECONDS):
                previous["copies"] += 1
                previous["last_at"] = now
                previous["last"] = stamp
                print("REPEAT", stamp, key, previous["copies"], digest.hex())
                return
            self.finish(topic, previous, "value_change" if not same else "window")
        elif len(self.feeds) >= MAX_FEEDS:
            oldest = min(self.feeds, key=lambda name: self.feeds[name]["last_at"])
            self.finish(oldest, self.feeds[oldest], "capacity")
            del self.feeds[oldest]
            self.evictions += 1
            print("TRACKING CAPACITY; evicted", oldest, "total", self.evictions)
        self.feeds[topic] = {
            "hash": digest, "bytes": len(value),
            "preview": value[:48].decode("utf-8", "replace"),
            "copies": 1, "start_at": now, "last_at": now,
            "first": stamp, "last": stamp,
        }

    def expire(self, now):
        expired = [
            topic for topic, state in self.feeds.items()
            if now - state["last_at"] > QUIET_SECONDS
            or now - state["start_at"] > WINDOW_SECONDS
        ]
        for topic in expired:
            self.finish(topic, self.feeds[topic], "quiet_or_window")
            del self.feeds[topic]

    def disconnected(self):
        for topic, state in self.feeds.items():
            self.finish(topic, state, "disconnect")
        self.feeds.clear()
        print("CAPTURE GAP: disconnected; detection baselines cleared")


class MqttDiagnosticLogger:
    def debug(self, message, *args):
        pass

    def info(self, message, *args):
        print("MQTT LIB:", message % args if args else message)

    warning = info
    error = info
    critical = info


def print_exception(exc):
    print("NETWORK:", type(exc).__name__, repr(exc))
    for _ in range(4):
        cause = getattr(exc, "__cause__", None)
        if cause is None:
            cause = getattr(exc, "__context__", None)
        if cause is None or cause is exc:
            break
        print("CAUSED BY:", type(cause).__name__, repr(cause))
        exc = cause


def on_connect(client, userdata, flags, result):
    client.subscribe(username + "/feeds/+", qos=0)
    client.subscribe(username + "/throttle", qos=0)
    client.subscribe(username + "/errors", qos=0)
    print("MQTT CONNECTED: base-feed, throttle and error subscriptions completed")


def on_message(client, topic, message):
    global pause_until
    if topic == username + "/throttle":
        pause_until = time.monotonic() + 60
        print("THROTTLE", timestamp_string(), message)
    elif topic == username + "/errors":
        print("BROKER ERROR", timestamp_string(), message)
    else:
        monitor.receive(topic, message, time.monotonic(), timestamp_string())


def remaining_capacity(info):
    if not isinstance(info, dict):
        raise ValueError("Rate response is not an object")
    limit = info.get("data_rate_limit")
    active = info.get("active_data_rate")
    if type(limit) is not int or type(active) is not int or limit <= 0 or active < 0:
        raise ValueError("Invalid account rate fields")
    return limit - active


username = os.getenv("aio_username")
led_pin = digitalio.DigitalInOut(board.LED)
led_pin.switch_to_output(value=False)
status_led = StatusLed(led_pin, time.monotonic())
key = os.getenv("aio_key")
if not username or not key:
    raise ValueError("settings.toml must define aio_username and aio_key")
wifi.radio.connect(
    os.getenv("CIRCUITPY_WIFI_SSID"), os.getenv("CIRCUITPY_WIFI_PASSWORD")
)
pool = socketpool.SocketPool(wifi.radio)
io = IO_HTTP(
    username, key, adafruit_requests.Session(pool, ssl.create_default_context())
)
rtc.RTC().datetime = time.struct_time(io.receive_time(os.getenv("TIMEZONE")))
monitor = DuplicateMonitor(username)
mqtt = MQTT(
    broker="io.adafruit.com", port=8883, username=username, password=key,
    is_ssl=True, socket_pool=pool, ssl_context=ssl.create_default_context(),
    socket_timeout=1, recv_timeout=10, connect_retries=1,
)
mqtt.on_connect = on_connect
mqtt.on_message = on_message
mqtt.logger = MqttDiagnosticLogger()
ready = False
pause_until = 0
last_connect = last_send_attempt = -100
last_health = time.monotonic()
mqtt_failures = write_failures = rate_failures = 0
print("DUPLICATE MONITOR STARTED", timestamp_string())
monitor.queue({"kind": "startup", "timestamp": timestamp_string(), "max_feeds": MAX_FEEDS})

while True:
    now = time.monotonic()
    status_led.update(now)
    if not ready and now - last_connect >= RETRY_SECONDS:
        last_connect = now
        try:
            if not wifi.radio.connected:
                wifi.radio.connect(
                    os.getenv("CIRCUITPY_WIFI_SSID"),
                    os.getenv("CIRCUITPY_WIFI_PASSWORD"),
                )
            mqtt.connect()
            ready = True
        except (MMQTTException, OSError, RuntimeError, ValueError) as exc:
            mqtt_failures += 1
            print_exception(exc)
            if mqtt.is_connected():
                try:
                    mqtt.disconnect()
                except (MMQTTException, OSError, RuntimeError) as close_error:
                    print_exception(close_error)
            last_connect = time.monotonic()

    if ready:
        try:
            mqtt.loop(timeout=1)
        except (MMQTTException, OSError, RuntimeError) as exc:
            mqtt_failures += 1
            print_exception(exc)
            ready = False
            monitor.disconnected()
            if mqtt.is_connected():
                try:
                    mqtt.disconnect()
                except (MMQTTException, OSError, RuntimeError) as close_error:
                    print_exception(close_error)
            last_connect = time.monotonic()

    now = time.monotonic()
    monitor.expire(now)
    if (ready and monitor.reports and now >= pause_until
            and now - last_send_attempt >= SEND_SECONDS):
        last_send_attempt = now
        try:
            remaining = remaining_capacity(io.get_user_rate_info())
        except (AdafruitIO_RequestError, AdafruitIO_ThrottleError,
                OSError, RuntimeError, ValueError) as exc:
            rate_failures += 1
            print_exception(exc)
            if isinstance(exc, AdafruitIO_ThrottleError):
                pause_until = time.monotonic() + 60
        else:
            if remaining < 5:
                pause_until = time.monotonic() + 60
                print("DEFER REPORT: remaining capacity", remaining)
            else:
                # Drain pending MQTT notifications after the blocking rate request.
                try:
                    mqtt.loop(timeout=1)
                except (MMQTTException, OSError, RuntimeError) as exc:
                    mqtt_failures += 1
                    print_exception(exc)
                    ready = False
                    monitor.disconnected()
                    last_connect = time.monotonic()
                    if mqtt.is_connected():
                        try:
                            mqtt.disconnect()
                        except (MMQTTException, OSError, RuntimeError) as close_error:
                            print_exception(close_error)
                if ready and time.monotonic() >= pause_until:
                    payload = json.dumps(monitor.reports[0])
                    if len(payload.encode("utf-8")) > 1024:
                        raise ValueError("Burst report exceeds 1024 bytes")
                    try:
                        io.send_data(OUTPUT_FEED, payload)
                    except (AdafruitIO_RequestError, AdafruitIO_ThrottleError,
                            OSError, RuntimeError, ValueError) as exc:
                        write_failures += 1
                        print_exception(exc)
                        pause_until = time.monotonic() + 60
                    else:
                        monitor.reports.pop(0)
                        status_led.sent(time.monotonic())
                        print("SENT", OUTPUT_FEED, payload)
                    last_send_attempt = time.monotonic()

    now = time.monotonic()
    if now - last_health >= 60:
        gc.collect()
        print("HEALTH", timestamp_string(), json.dumps({
            "mqtt": ready, "messages": monitor.messages, "bursts": monitor.bursts,
            "tracked": len(monitor.feeds), "queue": len(monitor.reports),
            "report_drop": monitor.report_drops, "evicted": monitor.evictions,
            "invalid_topics": monitor.invalid_topics, "mqtt_fail": mqtt_failures,
            "write_fail": write_failures, "rate_fail": rate_failures,
            "heap": gc.mem_free(),
        }))
        last_health = now
    status_led.update(time.monotonic())
    time.sleep(0.05)
