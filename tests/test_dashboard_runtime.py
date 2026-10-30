"""Verify restart persistence and live multi-screen output through production C/BSP code."""
import csv
import io
import tempfile
import unittest
from pathlib import Path
from tools.dashboard_sim.runner import Host, build_host, run_scenario
from tools.can_sim.scenario import Scenario


class DashboardRuntimeTests(unittest.TestCase):
    """Use actual renderer and journal serialization, with file-backed fake eMMC only."""

    @classmethod
    def setUpClass(cls):
        """Build the sanitizer host once for all runtime tests."""
        cls.executable = build_host()

    def test_fault_history_survives_host_restart_and_telemetry_session_reset(self):
        """Saved events survive a restart unchanged and the next boot appends with a new boot ID."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            storage = root / "card.bin"
            with Host(self.executable, storage) as host:
                first = host.command("T 10 20 1 127 1 0")
                self.assertEqual(first["fault_count"], 1)
                self.assertEqual(first["fault_pending"], 0)
                host.command("T 20 20 1 127 0 0")
                # Recovery appends a CLEAR once the hold time has passed; it never erases.
                self.assertEqual(host.command("T 5019 20 1 127 0 0")["fault_count"], 1)
                self.assertEqual(host.command("T 5020 20 1 127 0 0")["fault_count"], 2)
                host.artifact("H", root / "first.csv")
            with Host(self.executable, storage) as host:
                # New boot, same malfunction. New record, same old immutable prefix.
                second = host.command("T 10 20 1 127 1 0")
                self.assertEqual(second["fault_count"], 3)
                host.artifact("H", root / "second.csv")
            first_rows = list(csv.DictReader(io.StringIO((root / "first.csv").read_text())))
            second_rows = list(csv.DictReader(io.StringIO((root / "second.csv").read_text())))
            self.assertEqual(first_rows, second_rows[:2])
            self.assertEqual([row["state"] for row in second_rows], ["2", "1", "2"])
            self.assertEqual(second_rows[0]["boot"], "1")
            self.assertEqual(second_rows[2]["boot"], "2")

    def test_partial_journal_slot_is_not_overwritten(self):
        """A torn slot on the card keeps its bytes and number; later events are saved after it."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            storage = root / "card.bin"
            journal_offset = 262144 * 512  # fake card maps physical block4096 to file0
            with storage.open("wb") as card:
                card.seek(journal_offset)
                card.write(b"interrupted slot")
            with Host(self.executable, storage) as host:
                result = host.command("T 10 20 1 127 1 0")
                # A torn first write keeps its slot and number; the IMD fault is saved after it.
                self.assertEqual(result["fault_count"], 2)
                self.assertEqual(result["fault_pending"], 0)
                self.assertEqual(result["journal"], 0)
                host.artifact("H", root / "history.csv")
            rows = list(csv.DictReader(io.StringIO((root / "history.csv").read_text())))
            self.assertEqual([row["sequence"] for row in rows], ["1", "2"])
            self.assertEqual(rows[0]["description"], "unreadable record")
            self.assertEqual((rows[1]["source"], rows[1]["description"]), ("IMD", "IMD: fault"))
            with Host(self.executable, storage) as host:
                # The next boot continues after both slots and still never rewrites slot one.
                self.assertEqual(host.command("T 10 20 1 127 0 0")["fault_count"], 2)
                self.assertEqual(host.command("T 20 20 1 127 1 0")["fault_count"], 3)
            with storage.open("rb") as card:
                card.seek(journal_offset)
                self.assertEqual(card.read(16), b"interrupted slot")

    def test_silent_bms_for_ten_minutes_is_one_entry_and_real_faults_still_recorded(self):
        """Regression: per-timeout entries once filled the whole journal in 7.5 minutes."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with Host(self.executable, root / "card.bin") as host:
                # IMD wired and healthy, other inputs unwired, BMS never answers.
                for tick in range(0, 600_001, 100):
                    result = host.command(f"T {tick} -1 -1 1 0 0 1")
                self.assertEqual(result["fault_count"], 1)
                self.assertGreater(result["data"]["pack_voltage_deci_volts"], -1)
                result = host.command("T 600100 -1 -1 1 1 0 1")
                self.assertEqual(result["fault_count"], 2)
                host.artifact("H", root / "history.csv")
            rows = list(csv.DictReader(io.StringIO((root / "history.csv").read_text())))
            self.assertEqual([row["description"] for row in rows],
                             ["BMS link: no reply since boot", "IMD: fault"])
            self.assertEqual(rows[0]["uptime_ms"], "10000")

    def test_unwired_inputs_never_fill_history_across_boots(self):
        """Boots without auxiliary providers add only the BMS link entry, never unwired inputs."""
        with tempfile.TemporaryDirectory() as directory:
            storage = Path(directory) / "card.bin"
            for _ in range(3):
                with Host(self.executable, storage) as host:
                    for tick in range(0, 15_001, 100):
                        # Production firmware has no auxiliary provider: all unknown, none wired.
                        result = host.command(f"T {tick} -1 -1 0 0 0 0")
            self.assertEqual(result["fault_count"], 3)  # One "no reply" BMS entry per boot only.

    def test_touch_input_navigates_every_screen(self):
        """Simulated touch through the host reaches every screen and back, including long presses."""
        with Host(self.executable) as host:
            tick = 0

            def step(milliseconds):
                """Advance the host clock in 10 ms steps and return the last step result."""
                nonlocal tick
                result = None
                for _ in range(milliseconds // 10):
                    tick += 10
                    result = host.command(f"T {tick} -1 -1 0 0 0 0")
                return result

            def tap(x, y, hold_ms=40):
                """Press, hold, release and settle; return the active screen name."""
                self.assertTrue(host.command(f"I {x} {y} 1")["ok"])
                step(hold_ms)
                self.assertTrue(host.command(f"I {x} {y} 0")["ok"])
                return step(200)["screen"]

            self.assertEqual(step(10)["screen"], "main")
            self.assertEqual(tap(240, 136, hold_ms=600), "debug")  # long press, as on the board
            self.assertEqual(tap(48, 31), "pack")
            self.assertEqual(tap(126, 31), "faults")
            self.assertEqual(tap(48, 31), "debug")
            self.assertEqual(tap(126, 31), "faults")
            self.assertEqual(tap(204, 31), "main")
            self.assertEqual(tap(240, 136, hold_ms=600), "debug")
            self.assertEqual(tap(300, 180, hold_ms=600), "main")
            self.assertEqual(tap(240, 136, hold_ms=600), "debug")
            self.assertEqual(tap(48, 31), "pack")
            self.assertEqual(tap(300, 180, hold_ms=600), "main")
        # Off-screen touch points are rejected rather than clamped onto a button.
        with self.assertRaises(RuntimeError), Host(self.executable) as host:
            host.command("I 480 10 1")

    def test_all_screens_render_from_runtime_inputs_and_export_history(self):
        """All four screens render from scenario inputs and the history exports as CSV."""
        scenario = Scenario.from_dict({"duration": .1, "signals": {"pack_voltage_v": 400},
            "auxiliary": {"speed_kph": 12, "drive_mode": "D", "statuses": {"imd": "fault"}}})
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_scenario(scenario, root, self.executable, capture_every_ms=100)
            for view in ("debug", "pack", "faults"):
                file = root / f"frame-00000100-{view}.png"
                self.assertTrue(file.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))
            rows = list(csv.DictReader(io.StringIO((root / "fault-history.csv").read_text())))
            self.assertTrue(any(row["source"] == "IMD" for row in rows))
            html = (root / "index.html").read_text()
            self.assertIn('id="view"', html)
            self.assertIn('frame-00000100-faults.png', html)


if __name__ == "__main__":
    unittest.main()
