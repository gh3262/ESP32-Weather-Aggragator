"""Exercise the actual monitor loop with fake board/network interfaces."""
import ast
import json
from pathlib import Path
from types import SimpleNamespace
import unittest


class RequestError(Exception):
    pass


class ThrottleError(Exception):
    pass


class MonitorTests(unittest.TestCase):
    def setUp(self):
        path = Path(__file__).resolve().parents[1] / "aio-status" / "code.py"
        tree = ast.parse(path.read_text())
        self.tree = tree
        self.now = 0
        self.writes = []
        self.output = []
        self.rate = {"data_rate_limit": 60, "active_data_rate": 10}
        self.read_error = self.write_error = None
        self.mqtt_error = None
        self.subscriptions = []
        clock = SimpleNamespace(
            tm_year=2026, tm_mon=10, tm_mday=25,
            tm_hour=9, tm_min=0, tm_sec=0,
        )
        self.mqtt = SimpleNamespace(
            connect=self.connect, loop=self.mqtt_loop,
            disconnect=lambda: None, subscribe=self.subscriptions.append,
            is_connected=lambda: False,
        )
        self.ns = {
            "time": SimpleNamespace(monotonic=lambda: self.now, sleep=lambda _: None),
            "rtc": SimpleNamespace(RTC=lambda: SimpleNamespace(datetime=clock)),
            "gc": SimpleNamespace(mem_free=lambda: 100000, collect=lambda: None),
            "json": json,
            "print": lambda *args: self.output.append(args),
            "MMQTTException": ConnectionError,
            "AdafruitIO_RequestError": RequestError,
            "AdafruitIO_ThrottleError": ThrottleError,
            "mqtt": self.mqtt,
            "wifi": SimpleNamespace(radio=SimpleNamespace(connected=True)),
            "io": SimpleNamespace(
                get_user_rate_info=self.get_rate, send_data=self.send_data
            ),
            "aio_username": "test-user",
            "bp": [0],
        }
        nodes = []
        initializing = False
        for node in tree.body:
            if isinstance(node, (ast.Import, ast.ImportFrom)):
                continue
            if isinstance(node, ast.Assign):
                name = node.targets[0]
                if isinstance(name, ast.Name) and name.id == "bp":
                    break
            nodes.append(node)
        for node in tree.body:
            if isinstance(node, ast.Assign):
                name = node.targets[0]
                if isinstance(name, ast.Name) and name.id == "events":
                    initializing = True
            if isinstance(node, ast.While):
                self.loop = compile(ast.Module(body=node.body, type_ignores=[]), str(path), "exec")
                break
            if initializing:
                nodes.append(node)
        exec(compile(ast.Module(body=nodes, type_ignores=[]), str(path), "exec"), self.ns)

    def connect(self):
        self.ns["on_connect"](self.mqtt, None, None, 0)

    def mqtt_loop(self, timeout):
        self.assertEqual(timeout, 1)
        if self.mqtt_error:
            raise self.mqtt_error

    def get_rate(self):
        if self.read_error:
            raise self.read_error
        return self.rate

    def send_data(self, feed, payload):
        if self.write_error:
            raise self.write_error
        self.writes.append((self.now, feed, payload))

    def tick(self, now):
        self.now = now
        exec(self.loop, self.ns)

    def test_single_poll_and_compatible_status(self):
        calls = []
        original = self.ns["io"].get_user_rate_info
        self.ns["io"].get_user_rate_info = lambda: calls.append(1) or original()
        for now in range(1, 121):
            self.tick(now)
        self.assertEqual(len(calls), 120)
        statuses = [p for _, f, p in self.writes if f == "aggregator.aio-status"]
        self.assertEqual(len(statuses), 4)
        self.assertEqual(statuses[0], "20261025090000,50,50.0,30,50")
        self.assertEqual(self.subscriptions, ["test-user/throttle", "test-user/errors"])
        self.assertTrue(all(b[0] - a[0] >= 2 for a, b in zip(self.writes, self.writes[1:])))
        self.assertLessEqual(len(self.writes), 8)

    def test_rate_validation_and_optional_fields(self):
        for bad in (None, [], {}, {"data_rate_limit": 0, "active_data_rate": 1},
                    {"data_rate_limit": 60, "active_data_rate": -1},
                    {"data_rate_limit": True, "active_data_rate": 0},
                    {"data_rate_limit": 60, "active_data_rate": "1"}):
            with self.assertRaises(ValueError):
                self.ns["read_remaining"](bad)
        self.rate.update(authentication_rate=3, mqtt_ban_error_message="example")
        self.tick(1)
        payload = json.loads(self.ns["pending_rate"])
        self.assertEqual(payload["rates"]["authentication_rate"], 3)
        self.assertEqual(payload["rates"]["mqtt_ban_error_message"], "example")
        self.assertLessEqual(len(self.ns["pending_rate"].encode()), 1024)

    def test_throttle_and_exhaustion_defer_writes(self):
        self.tick(1)
        self.ns["on_message"](self.mqtt, "test-user/throttle", "throttle detail")
        self.rate["active_data_rate"] = 60
        for now in range(2, 66):
            self.tick(now)
        self.assertEqual(len(self.writes), 1)  # Only the pre-throttle startup event.
        self.assertGreater(self.ns["status_drops"], 0)
        self.rate["active_data_rate"] = 10
        self.tick(124)
        self.assertEqual(len(self.writes), 1)
        self.tick(125)
        self.assertEqual(self.writes[-1][1], "aggregator.aio-status")
        self.assertEqual(self.ns["throttle_count"], 1)

    def test_failures_missing_samples_and_retained_retry(self):
        self.read_error = RequestError("HTTP failure")
        for now in range(1, 31):
            self.tick(now)
        self.assertFalse(self.writes)
        self.assertIsNone(self.ns["pending_status"])
        self.assertEqual(self.ns["poll_failures"], 30)
        self.assertEqual(len(self.ns["events"]), self.ns["MAX_EVENTS"])
        self.assertGreater(self.ns["event_drops"], 0)
        self.read_error = None
        self.write_error = ThrottleError("write throttle")
        self.tick(31)
        self.assertEqual(self.ns["write_failures"], 1)
        self.assertEqual(self.ns["pause_until"], 91)
        self.write_error = None
        for now in range(32, 91):
            self.tick(now)
        self.assertFalse(self.writes)
        self.tick(91)
        self.assertEqual(self.writes[-1][1], "aggregator.aio-status")

    def test_bounded_events_full_serial_and_mqtt_reconnect(self):
        self.tick(1)
        text = "x" * 1000
        self.ns["on_message"](self.mqtt, "test-user/errors", text)
        self.assertEqual(self.ns["error_count"], 1)
        event = self.ns["events"][-1]
        self.assertEqual(len(event["detail"]), 120)
        self.assertTrue(any(text in line for line in self.output))
        self.mqtt_error = ConnectionError("lost")
        self.tick(2)
        self.assertFalse(self.ns["mqtt_ready"])
        self.mqtt_error = None
        self.tick(16)
        self.assertFalse(self.ns["mqtt_ready"])
        self.tick(17)
        self.assertTrue(self.ns["mqtt_ready"])
        self.assertEqual(len(self.subscriptions), 4)

    def test_poll_throttle_and_no_publish_from_callback(self):
        self.tick(1)
        count = len(self.writes)
        self.ns["on_message"](self.mqtt, "test-user/errors", "bad publish")
        self.assertEqual(len(self.writes), count)
        self.read_error = ThrottleError("HTTP 429")
        self.tick(2)
        self.assertEqual(self.ns["pause_until"], 62)
        self.assertEqual(self.ns["poll_failures"], 1)

    def test_repeated_connection_failures_coalesce(self):
        self.ns["on_connect"] = lambda *args: (_ for _ in ()).throw(
            ConnectionError("connect failed")
        )
        for now in (1, 16, 31, 46):
            self.read_error = RequestError("offline")
            self.tick(now)
        failures = [e for e in self.ns["events"] if e["kind"] == "mqtt_connect_failed"]
        self.assertEqual(len(failures), 1)
        self.assertEqual(failures[0]["repeats"], 4)
        self.assertEqual(self.ns["mqtt_failures"], 4)
        self.assertEqual(len([line for line in self.output if line[0] == "MQTT exception:"]), 4)

    def test_exception_diagnostics_without_sys_print_exception(self):
        self.ns["print_mqtt_exception"](RequestError("plain failure"))
        outer = ConnectionError("Connect failure")
        outer.__cause__ = OSError("socket timed out")
        self.ns["print_mqtt_exception"](outer)
        logger = self.ns["MqttDiagnosticLogger"]()
        before = len(self.output)
        logger.debug("must not print packet contents")
        self.assertEqual(len(self.output), before)
        logger.warning("Socket error when connecting: %s", "timeout")
        self.assertTrue(any("socket timed out" in str(line) for line in self.output))
        self.assertIn(("MQTT LIB:", "Socket error when connecting: timeout"), self.output)

    def test_connection_and_loop_share_default_socket_timeout(self):
        constructor = next(
            node.value for node in self.tree.body
            if isinstance(node, ast.Assign)
            and isinstance(node.targets[0], ast.Name)
            and node.targets[0].id == "mqtt"
        )
        options = {keyword.arg: keyword.value for keyword in constructor.keywords}
        self.assertEqual(options["socket_timeout"].id, "MQTT_SOCKET_TIMEOUT")
        self.assertEqual(self.ns["MQTT_SOCKET_TIMEOUT"], 1)
        self.assertEqual(options["recv_timeout"].value, 10)
        self.assertEqual(options["connect_retries"].value, 1)

    def test_headroom_and_stale_samples_block_writes(self):
        self.rate["active_data_rate"] = 56
        for now in range(1, 31):
            self.tick(now)
        self.assertFalse(self.writes)
        self.assertIsNotNone(self.ns["pending_status"])
        self.read_error = RequestError("offline")
        self.tick(40)
        self.assertFalse(self.writes)
        self.read_error = None
        self.rate["active_data_rate"] = 55
        self.tick(41)
        self.assertEqual(len(self.writes), 1)
        self.assertEqual(self.writes[0][1], "aggregator.aio-status")
        self.assertIn(",4,4.0,30,4", self.writes[0][2])

    def test_event_payload_size_and_unknown_optional_fields(self):
        for key in ("poll_failures", "write_failures", "mqtt_failures",
                    "throttle_count", "error_count", "event_drops", "status_drops"):
            self.ns[key] = 4294967295
        info = {
            "data_rate_limit": 60, "active_data_rate": 10,
            "authentication_rate": 4294967295,
            "subscribe_authorization_rate": 4294967295,
            "publish_authorization_rate": 4294967295,
            "hourly_ban_rate": 4294967295,
            "active_sms_rate": 4294967295,
            "mqtt_ban_error_message": "\u2603" * 1000,
            "unexpected": "not forwarded",
        }
        encoded = self.ns["make_rate_event"](info)
        self.assertLessEqual(len(encoded.encode("utf-8")), 1024)
        self.assertNotIn("unexpected", json.loads(encoded)["rates"])
        self.ns["queue_event"]("broker_error", "\u2603" * 1000)
        self.assertLessEqual(len(json.dumps(self.ns["events"][-1]).encode()), 1024)


if __name__ == "__main__":
    unittest.main()
