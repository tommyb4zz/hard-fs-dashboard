"""Drive the live dashboard over real HTTP: page, frames, state and touch navigation."""
import json
import threading
import time
import unittest
import urllib.error
import urllib.request

from tools.can_sim.scenario import Scenario
from tools.dashboard_sim.live import LiveDashboard, make_server
from tools.dashboard_sim.runner import ROOT, build_host


class LiveDashboardTests(unittest.TestCase):
    """Time is stepped explicitly so touch timing is deterministic; one test uses the clock."""

    @classmethod
    def setUpClass(cls):
        """Build the sanitizer host once for every live run."""
        cls.executable = build_host()
        cls.scenario = Scenario.from_file(ROOT / "tools/can_sim/scenarios/dashboard.json")

    def setUp(self):
        """Start a manually stepped dashboard behind a loopback server on a free port."""
        self.dashboard = LiveDashboard(self.executable, self.scenario)
        self.dashboard.start(real_time=False)
        self.server = make_server(self.dashboard, 0)
        self.base = f"http://127.0.0.1:{self.server.server_address[1]}"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        """Stop serving, then stop the host and check its exit status."""
        self.server.shutdown()
        self.server.server_close()
        self.dashboard.stop()

    def get(self, path):
        """Return status, content type and body of one GET."""
        with urllib.request.urlopen(self.base + path, timeout=10) as response:
            return response.status, response.headers["Content-Type"], response.read()

    def touch(self, x, y, pressed):
        """POST one touch press or release and return the status code."""
        request = urllib.request.Request(f"{self.base}/touch?x={x}&y={y}&pressed={int(pressed)}",
                                         method="POST")
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                return response.status
        except urllib.error.HTTPError as error:
            return error.code

    def tap(self, x, y, hold_ms=40):
        """Press, hold for simulated time, release and settle; return the active screen."""
        self.assertEqual(self.touch(x, y, True), 204)
        self.dashboard.step(hold_ms)
        self.assertEqual(self.touch(x, y, False), 204)
        self.dashboard.step(200)
        return json.loads(self.get("/state")[2])["screen"]

    def test_page_frame_and_state_are_served(self):
        """The page, a 480x272 PNG frame and the state JSON are served."""
        status, kind, body = self.get("/")
        self.assertEqual((status, kind), (200, "text/html; charset=utf-8"))
        self.assertIn(b'id="screen"', body)
        self.dashboard.step(500)
        status, kind, body = self.get("/frame.png")
        self.assertEqual((status, kind), (200, "image/png"))
        self.assertTrue(body.startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertEqual(body[16:24], (480).to_bytes(4, "big") + (272).to_bytes(4, "big"))
        state = json.loads(self.get("/state")[2])
        self.assertEqual(state["screen"], "main")
        self.assertEqual(state["tick_ms"], 500)
        self.assertEqual(state["journal"], "saved to card")

    def test_touch_presses_run_the_firmware_navigation(self):
        """Taps and a long press over HTTP move through main, cells, pack and history like the board."""
        self.dashboard.step(100)
        self.assertEqual(self.tap(240, 136, hold_ms=600), "debug")
        self.assertEqual(self.tap(48, 31), "pack")
        self.assertEqual(self.tap(126, 31), "faults")
        self.assertEqual(self.tap(204, 31), "main")
        self.assertEqual(self.touch(999, 10, True), 400)
        self.assertEqual(self.touch(10, 10, False), 204)

    def test_frames_change_with_live_values(self):
        """Frames are rendered live: later simulated values change the pixels."""
        self.dashboard.step(1000)
        first = self.get("/frame.png")[2]
        self.dashboard.step(4000)
        self.assertNotEqual(first, self.get("/frame.png")[2])

    def test_unknown_paths_are_rejected(self):
        """Unknown paths are 404 and off-screen touch points are refused."""
        with self.assertRaises(urllib.error.HTTPError) as caught:
            self.get("/../../etc/passwd")
        self.assertEqual(caught.exception.code, 404)
        self.assertEqual(self.touch(-1, 10, True), 400)


class LiveClockTests(unittest.TestCase):
    """The real-time thread keeps simulated time near wall time."""

    def test_clock_advances_in_real_time(self):
        """The real-time thread keeps simulated time within a factor of wall time."""
        dashboard = LiveDashboard(build_host(),
                                  Scenario.from_file(ROOT / "tools/can_sim/scenarios/normal.json"))
        dashboard.start()
        try:
            time.sleep(1.0)
            elapsed = dashboard.state()["tick_ms"]
        finally:
            dashboard.stop()
        self.assertGreater(elapsed, 500)
        self.assertLess(elapsed, 2500)

    def test_instant_click_is_held_long_enough_for_lvgl(self):
        """A press released immediately (a fast mouse click) still reaches the button."""
        dashboard = LiveDashboard(build_host(),
                                  Scenario.from_file(ROOT / "tools/can_sim/scenarios/normal.json"))
        dashboard.start()
        try:
            dashboard.touch(240, 136, True)
            time.sleep(0.8)
            dashboard.touch(240, 136, False)
            time.sleep(0.4)
            self.assertEqual(dashboard.state()["screen"], "debug")
            dashboard.touch(48, 31, True)
            dashboard.touch(48, 31, False)  # no delay between press and release
            time.sleep(0.4)
            self.assertEqual(dashboard.state()["screen"], "pack")
        finally:
            dashboard.stop()


if __name__ == "__main__":
    unittest.main()
