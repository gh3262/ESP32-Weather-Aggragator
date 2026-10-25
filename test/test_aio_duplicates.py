"""Run the detector and production loop without CircuitPython hardware."""
import ast
import hashlib
import json
from pathlib import Path
from types import SimpleNamespace
import unittest


class RequestError(Exception):
    pass


class ThrottleError(Exception):
    pass


class DuplicateTests(unittest.TestCase):
    def setUp(self):
        path = Path(__file__).resolve().parents[1] / "aio-duplicates" / "code.py"
        tree = ast.parse(path.read_text())
        definitions = []
        for node in tree.body:
            if isinstance(node, (ast.Import, ast.ImportFrom)):
                continue
            if (isinstance(node, ast.Assign)
                    and isinstance(node.targets[0], ast.Name)
                    and node.targets[0].id == "username"):
                break
            definitions.append(node)
        self.now = 0
        self.logs = []
        self.writes = []
        self.rate_error = self.write_error = self.mqtt_error = None
        self.remaining = 60
        self.connected = False
        self.subscriptions = []
        clock = SimpleNamespace(
            tm_year=2026, tm_mon=10, tm_mday=25, tm_hour=12, tm_min=0, tm_sec=0
        )
        self.ns = {
            "json": json, "hashlib": SimpleNamespace(new=hashlib.new),
            "print": lambda *args: self.logs.append(args),
            "time": SimpleNamespace(monotonic=lambda: self.now, sleep=lambda _: None),
            "rtc": SimpleNamespace(RTC=lambda: SimpleNamespace(datetime=clock)),
            "gc": SimpleNamespace(collect=lambda: None, mem_free=lambda: 100000),
            "MMQTTException": ConnectionError,
            "AdafruitIO_RequestError": RequestError,
            "AdafruitIO_ThrottleError": ThrottleError,
        }
        exec(compile(ast.Module(body=definitions, type_ignores=[]), str(path), "exec"), self.ns)
        self.monitor = self.ns["DuplicateMonitor"]("test")
        self.led_pin = SimpleNamespace(value=False)
        self.led = self.ns["StatusLed"](self.led_pin, 0)
        mqtt = SimpleNamespace(
            connect=self.connect, loop=self.loop_mqtt,
            is_connected=lambda: self.connected, disconnect=self.disconnect,
            subscribe=lambda topic, qos: self.subscriptions.append((topic, qos)),
        )
        self.ns.update(
            monitor=self.monitor, username="test", mqtt=mqtt,
            wifi=SimpleNamespace(radio=SimpleNamespace(connected=True)),
            io=SimpleNamespace(get_user_rate_info=self.get_rate, send_data=self.send),
            ready=False, pause_until=0, last_connect=-100, last_send_attempt=-100,
            last_health=0, mqtt_failures=0, write_failures=0, rate_failures=0,
            status_led=self.led,
        )
        loop = next(node for node in tree.body if isinstance(node, ast.While))
        self.loop = compile(ast.Module(body=loop.body, type_ignores=[]), str(path), "exec")

    def connect(self):
        self.connected = True
        self.ns["on_connect"](self.ns["mqtt"], None, None, 0)

    def disconnect(self):
        self.connected = False

    def loop_mqtt(self, timeout):
        self.assertEqual(timeout, 1)
        if self.mqtt_error:
            raise self.mqtt_error

    def get_rate(self):
        if self.rate_error:
            raise self.rate_error
        return {"data_rate_limit": 60, "active_data_rate": 60 - self.remaining}

    def send(self, feed, payload):
        if self.write_error:
            raise self.write_error
        self.writes.append((self.now, feed, json.loads(payload)))

    def tick(self, now):
        self.now = now
        exec(self.loop, self.ns)

    def receive(self, topic, value, now):
        self.monitor.receive("test/feeds/" + topic, value, now, str(now))

    def test_interleaved_seventeen_copies(self):
        for index in range(17):
            now = index / 2
            self.receive("hours2humidity", "79", now)
            self.receive("other", str(index), now)
        self.monitor.expire(12)
        self.assertEqual(len(self.monitor.reports), 1)
        report = self.monitor.reports[0]
        self.assertEqual(report["copies"], 17)
        self.assertEqual(report["span_s"], 8)
        self.assertEqual(report["feed"], "hours2humidity")
        self.assertEqual(report["sha256"], hashlib.sha256(b"79").hexdigest())
        self.monitor.expire(20)
        self.assertEqual(self.monitor.bursts, 1)

    def test_nonblocking_heartbeat_and_three_send_flashes(self):
        self.led.update(4.99)
        self.assertFalse(self.led_pin.value)
        self.led.update(5)
        self.assertTrue(self.led_pin.value)
        self.led.update(5.1)
        self.assertTrue(self.led_pin.value)
        self.led.update(5.21)
        self.assertFalse(self.led_pin.value)
        self.led.sent(6)
        self.assertTrue(self.led_pin.value)
        for now, expected in ((6.3, False), (6.6, True), (6.9, False),
                              (7.2, True), (7.5, False)):
            self.led.update(now)
            self.assertEqual(self.led_pin.value, expected)
        self.assertEqual(self.led.edges_left, 0)
        self.led.update(11)
        self.assertTrue(self.led_pin.value)

    def test_slow_loop_preserves_flash_edges_and_failed_send_does_not_flash(self):
        self.led.sent(0)
        for now, expected in ((2, False), (4, True), (6, False), (8, True), (10, False)):
            self.led.update(now)
            self.assertEqual(self.led_pin.value, expected)
        self.monitor.queue({"kind": "startup"})
        self.write_error = RequestError("failed")
        self.tick(0)
        self.assertEqual(self.led.edges_left, 0)

    def test_normal_updates_two_copies_and_reversions(self):
        for now in (0, 60, 120):
            self.receive("steady", "79", now)
        self.receive("two", "same", 120)
        self.receive("two", "same", 121)
        for now, value in enumerate(("A", "B", "A"), 122):
            self.receive("revert", value, now)
        self.monitor.expire(200)
        self.assertEqual(self.monitor.bursts, 0)

    def test_quiet_window_and_value_changes_finalize(self):
        for now in (0, 1, 2):
            self.receive("one", "A", now)
        self.receive("one", "B", 3)
        self.assertEqual(self.monitor.reports[0]["end"], "value_change")
        for now in range(11):
            self.receive("continuous", "A", now)
        self.receive("continuous", "A", 11)
        self.assertEqual(self.monitor.reports[-1]["copies"], 11)
        self.assertEqual(self.monitor.reports[-1]["span_s"], 10)
        self.monitor.expire(15)
        self.assertEqual(self.monitor.bursts, 2)

    def test_excludes_own_feed_alternates_and_wrong_account(self):
        for now in range(4):
            self.receive("aggregator.aio-duplicates", "same", now)
            self.receive("test/json", "same", now)
            self.monitor.receive("other/feeds/test", "same", now, str(now))
        self.assertEqual(self.monitor.messages, 0)
        self.assertEqual(self.monitor.invalid_topics, 4)

    def test_bounded_memory_reports_payload_and_large_values(self):
        self.ns["MAX_FEEDS"] = 2
        self.ns["MAX_REPORTS"] = 1
        for feed in ("A", "B", "C"):
            for now in (0, 1, 2):
                self.receive(feed, "\u2603" * 1000, now)
        self.assertLessEqual(len(self.monitor.feeds), 2)
        self.assertEqual(self.monitor.evictions, 1)
        self.monitor.expire(6)
        self.assertEqual(self.monitor.bursts, 3)
        self.assertEqual(self.monitor.report_drops, 2)
        self.assertLessEqual(len(json.dumps(self.monitor.reports[0]).encode()), 1024)
        self.assertLessEqual(len(self.monitor.reports[0]["preview"]), 48)

    def test_disconnect_finalizes_and_clears(self):
        for now in range(3):
            self.receive("test", b"same", now)
        self.monitor.disconnected()
        self.assertFalse(self.monitor.feeds)
        self.assertEqual(self.monitor.reports[0]["end"], "disconnect")
        self.receive("test", "same", 10)
        self.assertEqual(self.monitor.feeds["test/feeds/test"]["copies"], 1)

    def test_large_values_same_preview_do_not_match(self):
        prefix = "x" * 2000
        for now, suffix in enumerate(("A", "B", "C")):
            self.receive("long", prefix + suffix, now)
        self.monitor.expire(6)
        self.assertFalse(self.monitor.reports)

    def test_maximum_key_and_escaped_preview_fit_report(self):
        feed = "a" * 128
        for now in range(3):
            self.receive(feed, "\x00" * 1000, now)
        self.monitor.expire(6)
        self.assertLessEqual(len(json.dumps(self.monitor.reports[0]).encode()), 1024)
        self.assertEqual(self.monitor.reports[0]["bytes"], 1000)

    def test_runtime_subscriptions_pacing_and_failed_write_retry(self):
        self.monitor.queue({"kind": "startup"})
        self.monitor.queue({"kind": "second"})
        self.tick(0)
        self.assertEqual(self.led.edges_left, 5)
        self.assertEqual(self.subscriptions, [
            ("test/feeds/+", 0), ("test/throttle", 0), ("test/errors", 0)
        ])
        self.tick(29)
        self.assertEqual(len(self.writes), 1)
        self.write_error = RequestError("ambiguous write")
        self.tick(30)
        self.assertEqual(self.ns["write_failures"], 1)
        self.assertEqual(len(self.monitor.reports), 1)
        self.write_error = None
        self.tick(89)
        self.assertEqual(len(self.writes), 1)
        self.tick(90)
        self.assertEqual(len(self.writes), 2)

    def test_runtime_headroom_throttle_and_rate_errors(self):
        self.monitor.queue({"kind": "startup"})
        self.remaining = 4
        self.tick(0)
        self.assertFalse(self.writes)
        self.remaining = 60
        self.tick(59)
        self.assertFalse(self.writes)
        self.ns["on_message"](None, "test/throttle", "throttled")
        self.tick(118)
        self.assertFalse(self.writes)
        self.rate_error = ThrottleError("HTTP 429")
        self.tick(119)
        self.assertEqual(self.ns["rate_failures"], 1)
        self.rate_error = None
        self.tick(179)
        self.assertEqual(len(self.writes), 1)

    def test_runtime_gap_reconnect_and_no_callback_writes(self):
        self.tick(0)
        for now in (0, 1, 2):
            self.now = now
            self.ns["on_message"](None, "test/feeds/x", "same")
        self.assertFalse(self.writes)
        self.mqtt_error = ConnectionError("lost")
        self.tick(3)
        self.assertFalse(self.ns["ready"])
        self.assertFalse(self.monitor.feeds)
        self.mqtt_error = None
        self.tick(17)
        self.assertFalse(self.ns["ready"])
        self.tick(18)
        self.assertTrue(self.ns["ready"])
        self.assertEqual(len(self.subscriptions), 6)
        self.assertEqual(self.writes[0][2]["copies"], 3)


if __name__ == "__main__":
    unittest.main()
