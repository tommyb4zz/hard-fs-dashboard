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
        cls.executable = build_host()

    def test_fault_history_survives_host_restart_and_telemetry_session_reset(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            storage = root / "card.bin"
            with Host(self.executable, storage) as host:
                first = host.command("T 10 20 1 127 1 0")
                self.assertEqual(first["fault_count"], 1)
                self.assertEqual(first["fault_pending"], 0)
                host.command("T 20 20 1 127 0 0")  # Recovery appends; never erases.
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
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            storage = root / "card.bin"
            journal_offset = 262144 * 512  # fake card maps physical block4096 to file0
            with storage.open("wb") as card:
                card.seek(journal_offset)
                card.write(b"interrupted slot")
            with Host(self.executable, storage) as host:
                result = host.command("T 10 20 1 127 1 0")
                # Both the IMD fault and unavailable/corrupt history storage are retained in RAM.
                self.assertEqual(result["fault_count"], 2)
                self.assertEqual(result["fault_pending"], 2)
            with storage.open("rb") as card:
                card.seek(journal_offset)
                self.assertEqual(card.read(16), b"interrupted slot")

    def test_all_screens_render_from_runtime_inputs_and_export_history(self):
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
