"""Host checks for the all-feed monitor's recovery paths."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "aio-all_feed" / "code.py"


class ResetRequested(Exception):
    pass


class FakeSession:
    def get(self, url, **kwargs):
        return url, kwargs

    def post(self, url, **kwargs):
        return url, kwargs


class WatchdogTests(unittest.TestCase):
    def setUp(self):
        self.feeds = 0
        self.connections = []
        self.now = 0
        tree = ast.parse(SOURCE.read_text(encoding="utf-8"))
        names = {
            "MonitorSession", "check_monitor_watchdog", "mqtt_connect",
            "is_feed_key", "on_message",
        }
        nodes = [
            node for node in tree.body
            if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in names
        ]
        self.ns = {
            "HTTP_TIMEOUT_SECONDS": 3,
            "NETWORK_STALL_SECONDS": 300,
            "adafruit_requests": SimpleNamespace(Session=FakeSession),
            "microcontroller": SimpleNamespace(
                watchdog=SimpleNamespace(feed=self.feed), reset=self.reset
            ),
            "print": lambda *args: None,
            "last_feed_message": 0,
            "summary_pending_since": None,
            "wifi": SimpleNamespace(radio=SimpleNamespace(
                connected=False, connect=lambda *args, **kw: self.connections.append(kw)
            )),
            "ssid": "test",
            "password": "test",
            "mqtt_client": SimpleNamespace(
                connect=lambda: self.connections.append("mqtt")
            ),
            "time": SimpleNamespace(monotonic=lambda: self.now),
            "aio_username": "test",
            "AGGREGATOR_FEED_KEY": "aggregator.weather-log",
            "FEED_KEY_CHARS": "abcdefghijklmnopqrstuvwxyz0123456789-.",
            "feed_counts": {},
            "get_timestamp": lambda: "20261028110000",
            "do1_label": SimpleNamespace(text=""),
        }
        exec(compile(ast.Module(body=nodes, type_ignores=[]), str(SOURCE), "exec"), self.ns)

    def feed(self):
        self.feeds += 1

    def reset(self):
        raise ResetRequested()

    def test_idle_deadline(self):
        self.ns["check_monitor_watchdog"](299.999)
        self.assertEqual(self.feeds, 1)
        with self.assertRaises(ResetRequested):
            self.ns["check_monitor_watchdog"](300)
        self.assertEqual(self.feeds, 1)

    def test_upload_deadline_despite_fresh_messages(self):
        self.ns.update(last_feed_message=390, summary_pending_since=100)
        self.ns["check_monitor_watchdog"](399.999)
        with self.assertRaises(ResetRequested):
            self.ns["check_monitor_watchdog"](400)

    def test_feed_activity_and_aliases(self):
        self.now = 200
        callback = self.ns["on_message"]
        callback(None, "test/feeds/currenttemperature", "70")
        self.assertEqual(self.ns["last_feed_message"], 200)
        self.assertEqual(self.ns["feed_counts"], {"currenttemperature": 1})
        self.now = 250
        for feed in ("CurrentTemperature", "123456", "aggregator.weather-log"):
            callback(None, "test/feeds/" + feed, "70")
        self.assertEqual(self.ns["last_feed_message"], 200)
        self.ns["check_monitor_watchdog"](499)

    def test_http_timeouts(self):
        session = self.ns["MonitorSession"]()
        for method in (session.get, session.post):
            self.assertEqual(method("url")[1]["timeout"], 3)
            self.assertEqual(method("url", timeout=2)[1]["timeout"], 2)

    def test_wifi_reconnect_before_mqtt(self):
        self.assertTrue(self.ns["mqtt_connect"]())
        self.assertEqual(self.connections, [{"timeout": 3}, "mqtt"])
        self.assertEqual(self.feeds, 1)

    def test_wifi_failure_is_reported_as_failure(self):
        def fail(*args, **kwargs):
            raise ConnectionError("unavailable")

        self.ns["wifi"].radio.connect = fail
        self.assertFalse(self.ns["mqtt_connect"]())
        self.assertEqual(self.connections, [])


if __name__ == "__main__":
    unittest.main()
