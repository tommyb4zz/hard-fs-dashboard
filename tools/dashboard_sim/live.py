"""Purpose: run the production C/LVGL dashboard live and drive it from a browser.

One host process advances in real time against the scenario's virtual BMS (final values are
held after the scenario ends). A local HTTP server shows the real framebuffer and forwards
presses to the host's simulated touch panel, so taps and long presses run the firmware's own
LVGL handlers. Unlike the recorded player, nothing here is pre-rendered.
"""
from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
import time
from urllib.parse import parse_qs, urlparse
import uuid

import can

from tools.can_sim.engine import Responder, Runner
from tools.can_sim.scenario import Scenario
from tools.can_sim.transport import open_bus
from .runner import Host, ppm_bytes_to_png

STEP_MS = 10
# LVGL reads the touch panel every LV_INDEV_DEF_READ_PERIOD (30 ms); a press must stay down for
# at least two reads or a fast mouse click would never be seen, just like a too-short tap.
MIN_PRESS_MS = 60
JOURNAL_NAMES = ("saved to card", "not saved: no card", "retrying card write",
                 "not saved: check card", "not saved: journal full")


class LiveDashboard:
    """Own one running host, its virtual BMS and the clock thread that steps them.

    Every host command goes through one lock: the stepping thread and HTTP handlers share
    the process's stdin/stdout. step() is public so tests can advance time explicitly
    instead of waiting for the real-time thread.
    """

    def __init__(self, executable: Path, scenario: Scenario, storage_file: Path | None = None):
        """Prepare, but do not start, a live run of scenario on executable.

        storage_file optionally persists the fake eMMC card across runs (journal and
        telemetry); without it each run starts with an empty card."""
        self.executable = executable
        self.scenario = scenario
        self.storage_file = storage_file
        self.lock = threading.Lock()
        self.tick_ms = 0
        self.press_tick_ms = None
        self.last = {}
        self._stop = threading.Event()
        self._thread = None
        self._resources = []
        self._frame_dir = tempfile.TemporaryDirectory(prefix="dashboard-live-")

    def start(self, real_time: bool = True) -> None:
        """Open the virtual CAN pair and host, take the first step and optionally start the clock."""
        channel = "dashboard-live-" + uuid.uuid4().hex
        responder_bus = open_bus(channel=channel)
        self.client = open_bus(channel=channel)
        self.host = Host(self.executable, self.storage_file)
        self._resources = [self.host, self.client, responder_bus]
        self.runner = Runner(responder_bus, Responder(self.scenario))
        self.step(0)
        if real_time:
            self._thread = threading.Thread(target=self._clock, name="dashboard-live-clock", daemon=True)
            self._thread.start()

    def _clock(self) -> None:
        """Keep simulated time level with wall time, catching up at most one second at once."""
        origin = time.monotonic() - self.tick_ms / 1000
        while not self._stop.is_set():
            target = int((time.monotonic() - origin) * 1000)
            if target - self.tick_ms > 1000:
                origin = time.monotonic() - self.tick_ms / 1000
                target = self.tick_ms + STEP_MS
            while self.tick_ms + STEP_MS <= target and not self._stop.is_set():
                self.step(STEP_MS)
            self._stop.wait(STEP_MS / 1000)

    def step(self, milliseconds: int) -> dict:
        """Advance by milliseconds in STEP_MS ticks exactly as run_scenario does; return the last state."""
        record = self.last
        with self.lock:
            for _ in range(max(1, milliseconds // STEP_MS) if milliseconds else 1):
                if milliseconds:
                    self.tick_ms += STEP_MS
                elapsed = self.tick_ms / 1000
                self.runner.step(elapsed)
                frames = []
                while len(frames) < 128:
                    frame = self.client.recv(0)
                    if frame is None:
                        break
                    frames.append(frame)
                record = self.host.step(self.tick_ms, self.scenario.auxiliary_at(elapsed), frames,
                                        self.scenario.auxiliary_wired)
                if record["request"] is not None:
                    request = record["request"]
                    self.client.send(can.Message(arbitration_id=request["id"], is_extended_id=False,
                                                 data=bytes.fromhex(request["data"]), check=True))
                    self.runner.step(elapsed)
            self.last = record
        return record

    def touch(self, x: int, y: int, pressed: bool) -> None:
        """Press or release the simulated touch panel at display pixel x, y.

        With the real-time clock running, a release waits (up to one second) until the press
        has been down for MIN_PRESS_MS of simulated time; explicitly stepped runs release at once."""
        if not (0 <= x < 480 and 0 <= y < 272):
            raise ValueError("touch point outside the 480x272 display")
        if not pressed and self.press_tick_ms is not None and self._thread is not None:
            deadline = time.monotonic() + 1.0
            while self.tick_ms - self.press_tick_ms < MIN_PRESS_MS and time.monotonic() < deadline:
                time.sleep(STEP_MS / 1000)
        with self.lock:
            self.host.command(f"I {x} {y} {1 if pressed else 0}")
            self.press_tick_ms = self.tick_ms if pressed else None

    def frame_png(self) -> bytes:
        """Capture the current framebuffer from the host and return it as PNG bytes."""
        path = Path(self._frame_dir.name) / "frame.ppm"
        with self.lock:
            self.host.artifact("P", path)
            data = path.read_bytes()
        return ppm_bytes_to_png(data)

    def state(self) -> dict:
        """Summarize the latest step for the status line and tests."""
        record = self.last
        journal = record.get("journal", 0)
        return {"tick_ms": self.tick_ms, "screen": record.get("screen", "other"),
                "fault_count": record.get("fault_count", 0),
                "journal": JOURNAL_NAMES[journal] if 0 <= journal < len(JOURNAL_NAMES) else "unknown"}

    def stop(self) -> None:
        """Stop the clock, close the host (checking its exit) and release the buses."""
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=5)
        errors = []
        for resource in self._resources:
            try:
                if isinstance(resource, Host):
                    resource.__exit__(None, None, None)
                else:
                    resource.shutdown()
            except Exception as error:  # Report after releasing every resource.
                errors.append(error)
        self._resources = []
        self._frame_dir.cleanup()
        if errors:
            raise errors[0]


def make_server(dashboard: LiveDashboard, port: int = 8765) -> ThreadingHTTPServer:
    """Bind a loopback-only HTTP server for dashboard; port 0 picks a free port."""
    page = Path(__file__).with_name("live.html").read_bytes()

    class Handler(BaseHTTPRequestHandler):
        """Serve the page, frames and state; accept touch presses."""

        def log_message(self, format, *args):
            """Keep the terminal quiet; errors still reach the client as HTTP status codes."""

        def reply(self, status: int, body: bytes, content_type: str) -> None:
            """Send one complete uncached response."""
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            """Return the page, the current frame or the state JSON."""
            path = urlparse(self.path).path
            if path in ("/", "/index.html"):
                self.reply(200, page, "text/html; charset=utf-8")
            elif path == "/frame.png":
                try:
                    self.reply(200, dashboard.frame_png(), "image/png")
                except (RuntimeError, OSError, ValueError) as error:
                    self.reply(503, str(error).encode(), "text/plain")
            elif path == "/state":
                self.reply(200, json.dumps(dashboard.state()).encode(), "application/json")
            else:
                self.reply(404, b"not found", "text/plain")

        def do_POST(self):
            """Apply /touch?x=&y=&pressed= to the simulated touch panel."""
            url = urlparse(self.path)
            if url.path != "/touch":
                self.reply(404, b"not found", "text/plain")
                return
            query = parse_qs(url.query)
            try:
                dashboard.touch(int(query["x"][0]), int(query["y"][0]), query["pressed"][0] == "1")
            except (KeyError, ValueError):
                self.reply(400, b"expected x, y and pressed within the display", "text/plain")
                return
            except RuntimeError as error:
                self.reply(503, str(error).encode(), "text/plain")
                return
            self.reply(204, b"", "text/plain")

    return ThreadingHTTPServer(("127.0.0.1", port), Handler)
