"""Verify editable inputs through a closed virtual-CAN loop and actual LVGL/logging code.

Build the sanitizer-enabled full host once, then run closed-loop production
CAN/UI/LVGL/storage with synthetic inputs. Check screen pixels and labels, export
counts, unknown/fault recovery, capture cadence and parser/output-path guards."""
import json
import os
import tempfile
import unittest
from pathlib import Path

from tools.dashboard_sim.runner import Host, build_host, run_scenario
from tools.can_sim.scenario import Scenario


class DashboardSimulationTests(unittest.TestCase):
    """Check production requests, decoded values, real labels/captures and storage round trips."""

    @classmethod
    def setUpClass(cls):
        """Build the sanitizer-enabled host target once for the integration cases.

        Call build_host once and store its executable for seven integration cases.
        This writes the shared host build with ASan/UBSan enabled; build failures propagate.
        The tests then create separate host processes and temporary artifact directories."""
        cls.executable = build_host()

    def test_user_timelines_reach_actual_screen_and_export(self):
        """Custom battery/auxiliary points must appear in the real screen and exported records.

        Run 1.2 seconds of known battery values, a speed ramp, mode step and mixed
        statuses through production C/LVGL. Assert labels/data/status color, storage counts,
        request/reply traffic and distinct valid PNG captures in a temporary output folder.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({"duration": 1.2, "signals": {
            "pack_voltage_v": 400, "pack_current_a": -100,
            "highest_temperature_c": 72, "pack_soc_pct": 80,
        }, "auxiliary": {
            "speed_kph": {"interpolation": "linear", "points": [{"t": 0, "value": 0}, {"t": 1, "value": 100}]},
            "drive_mode": {"points": [{"t": 0, "value": "N"}, {"t": 0.5, "value": "D"}]},
            "statuses": {"imd": "fault", "rtd": "ok"},
        }})
        with tempfile.TemporaryDirectory() as directory:
            summary = run_scenario(scenario, Path(directory), self.executable, capture_every_ms=500)
            rows = [json.loads(line) for line in (Path(directory)/"snapshots.jsonl").read_text().splitlines()]
            final = rows[-1]
            self.assertEqual(final["labels"]["speed"], "100")
            self.assertEqual(final["labels"]["drive_mode"], "D")
            self.assertEqual(final["labels"]["highest_temperature"], "72")
            self.assertEqual(final["data"]["pack_current_deci_amps"], -1000)
            # LVGL's configured RGB565 channels expand to this RGB value.
            self.assertEqual(final["status_panels"][0]["border_rgb"], 0xF60C08)
            self.assertFalse(final["status_panels"][0]["hidden"])
            self.assertEqual(summary["exported_records"], final["emmc_count"])
            self.assertGreater(summary["requests"], 10)
            self.assertGreater(summary["responses"], 10)
            pngs = sorted(Path(directory).glob("*.png"))
            self.assertGreaterEqual(len(pngs), 3)
            self.assertTrue(all(path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n") for path in pngs))
            self.assertNotEqual(pngs[0].read_bytes(), pngs[-1].read_bytes())
            self.assertTrue((Path(directory)/"index.html").exists())

    def test_unknown_and_fault_window_recover_without_replacing_the_demo(self):
        """Unknown auxiliary values and malformed CAN windows must retain explicit state and recover.

        Run speed known/null/recovery and IMD ok/unknown/fault with malformed CAN
        from 1 to 2 seconds. Assert actual dash labels, unknown color, no malformed-window
        updates, later recovery and retained voltage; this host path does not alter firmware demo.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({"duration": 3.5, "signals": {"pack_voltage_v": 400},
            "faults": [{"type": "short_dlc", "start": 1, "end": 2}],
            "auxiliary": {"speed_kph": {"points": [{"t": 0, "value": 10}, {"t": 1, "value": None}, {"t": 2, "value": 20}]},
                "drive_mode": None, "statuses": {"imd": {"points": [{"t": 0, "value": "ok"}, {"t": 1, "value": "unknown"}, {"t": 2, "value": "fault"}]}}}})
        with tempfile.TemporaryDirectory() as directory:
            run_scenario(scenario, Path(directory), self.executable, capture_every_ms=0)
            rows = [json.loads(line) for line in (Path(directory)/"snapshots.jsonl").read_text().splitlines()]
            by_tick = {row["tick_ms"]: row for row in rows}
            self.assertEqual(by_tick[1000]["labels"]["speed"], "-")
            self.assertEqual(by_tick[1000]["labels"]["drive_mode"], "-")
            self.assertEqual(by_tick[1000]["status_panels"][0]["border_rgb"], 0x838183)
            self.assertTrue(all(not row["updated"] for row in rows if 1100 <= row["tick_ms"] < 2000))
            self.assertTrue(any(row["updated"] for row in rows if row["tick_ms"] > 2000))
            self.assertEqual(rows[-1]["labels"]["speed"], "20")
            self.assertEqual(rows[-1]["data"]["pack_voltage_deci_volts"], 4000)

    def test_host_rejects_invalid_command_without_hardware(self):
        """The stdin protocol must fail explicitly rather than accepting out-of-range auxiliary data.

        Send speed 70000 in a raw T stdin command to the memory-only host. Require
        RuntimeError when C rejects it; context shutdown must preserve the original error.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        with self.assertRaises(RuntimeError):
            with Host(self.executable) as host:
                host.command("T 0 70000 0 127 0 0")

    def test_host_rejects_overflow_and_malformed_payload_tokens(self):
        """Oversized integers and single-character payloads must not wrap into valid commands.

        Send oversized tick/speed/mode/mask integers, odd/nonhex payloads and invalid
        metadata flags in separate host contexts. Every case must fail instead of wrapping.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        commands = ["T 0 4294967296 0 127 0 0", "T 0 0 4294967296 127 0 0",
                    "T 0 0 0 4294967296 0 0", "T 18446744073709551616 0 0 127 0 0",
                    "T 0 0 0 127 0 1\n7eb 0 0 0 f",
                    "T 0 0 0 127 0 1\n7eb 0 0 0 z",
                    "T 0 0 0 127 0 1\n7eb 4294967296 0 0 -"]
        for command in commands:
            with self.subTest(command=command), self.assertRaises(RuntimeError):
                with Host(self.executable) as host:
                    host.command(command)

    def test_output_cannot_overwrite_input_or_hard_link(self):
        """Artifact paths must reject input aliases before writing any run data.

        Protect JSON bytes placed at summary.json and then hard-linked to log.bin.
        Each run must fail with ValueError before writing and preserve the original bytes.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({"duration": 0.2})
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            source = output / "summary.json"
            original = b'{"duration": 0.2}'
            source.write_bytes(original)
            with self.assertRaises(ValueError):
                run_scenario(scenario, output, self.executable, protected_inputs=(source,))
            self.assertEqual(source.read_bytes(), original)
            source.unlink()
            source = output / "inputs.json"
            source.write_bytes(original)
            os.link(source, output / "log.bin")
            with self.assertRaises(ValueError):
                run_scenario(scenario, output, self.executable, protected_inputs=(source,))
            self.assertEqual(source.read_bytes(), original)

    def test_invalid_run_bounds_do_not_start_host(self):
        """Bad time/capture settings and excessive step counts fail before subprocess access.

        Pass zero step, NaN duration, negative capture interval and excessive step
        count with a nonexistent executable. Require ValueError before process access.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({"duration": 1})
        with tempfile.TemporaryDirectory() as directory:
            for arguments in ({"step_ms": 0}, {"duration": float("nan")},
                              {"capture_every_ms": -1}, {"duration": 1000.001}):
                with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                    run_scenario(scenario, Path(directory), Path("missing-host"), **arguments)

    def test_default_captures_follow_inputs_ten_times_per_second(self):
        """The default visual run must capture 100 ms changes and its irregular final timestamp.

        Run a 0.35-second speed ramp with default captures. Require ticks 0/100/200/300/350
        ms and different 100/350-ms PNG pixels, including the irregular final timestamp.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({"duration": 0.35, "auxiliary": {
            "speed_kph": {"interpolation": "linear", "points": [
                {"t": 0, "value": 0}, {"t": 0.35, "value": 35}]}}})
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            summary = run_scenario(scenario, output, self.executable)
            self.assertEqual(summary["captures"], 5)
            self.assertEqual(sorted(path.name for path in output.glob("*.png")), [
                "frame-00000000.png", "frame-00000100.png", "frame-00000200.png",
                "frame-00000300.png", "frame-00000350.png"])
            self.assertNotEqual((output / "frame-00000100.png").read_bytes(),
                                (output / "frame-00000350.png").read_bytes())



if __name__ == "__main__":
    unittest.main()
