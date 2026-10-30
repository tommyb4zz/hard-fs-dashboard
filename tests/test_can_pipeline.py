"""Pass real python-can virtual frames into the production C CAN driver under sanitizers.

Compile a strict sanitizer-enabled production C CAN replay adapter and connect
its input frames through real python-can virtual endpoints. Tests cover all PIDs,
statistics, malformed frames, timeline faults and recovery without physical CAN."""

import json
import shutil
import subprocess
import tempfile
import unittest
import uuid
from pathlib import Path

import can
from tools.can_sim.engine import Responder, Runner
from tools.can_sim.protocol import encode_response, make_request
from tools.can_sim.scenario import Scenario


class VirtualFirmwarePipelineTests(unittest.TestCase):
    """Verify firmware snapshots from frames transported by an in-process CAN bus."""

    @classmethod
    def setUpClass(cls):
        """Compile the replay adapter against production source with address/UB checks.

        No external inputs. Locate clang/cc, create a temporary executable and compile
        the production-driver replay adapter with strict warnings and ASan/UBSan. Store
        class paths for later tests; missing compiler or failed compilation raises."""
        cls.root = Path(__file__).resolve().parents[1]
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            raise RuntimeError("A host C compiler is required for production-driver integration tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="can-firmware-pipeline-")
        cls.executable = Path(cls.temporary.name) / "can_replay"
        subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
                        "-I" + str(cls.root / "tests/can_host/include"),
                        "-I" + str(cls.root / "Inc"),
                        str(cls.root / "tests/can_host/test_app_can.c"),
                        "-o", str(cls.executable)], check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        """Remove the test executable after the production driver checks complete.

        Use the class-owned TemporaryDirectory created by setUpClass; delete its
        compiler output and return None after all replay tests. No hardware state is reset."""
        cls.temporary.cleanup()

    def replay_bus(self, frames):
        """Transport each timed frame through python-can, then decode its received bytes in C.

        Input frames is an ordered list of (tick_ms, Message) pairs. Open two unique
        same-process endpoints, send/receive each frame and assert receipt, then pass the
        received metadata/bytes to decode_frames. Return C snapshots; endpoints close on exit."""
        channel = "firmware-pipeline-" + uuid.uuid4().hex
        received_frames = []
        with can.Bus(interface="virtual", channel=channel, ignore_config=True) as sender, can.Bus(interface="virtual", channel=channel, ignore_config=True) as receiver:
            for tick, frame in frames:
                sender.send(frame)
                received = receiver.recv(0.5)
                self.assertIsNotNone(received)
                received_frames.append((tick, received))
        return self.decode_frames(received_frames)

    def decode_frames(self, frames):
        """Serialize received frame metadata and clock times into the production-driver adapter.

        Input frames pairs millisecond ticks with received Messages. Serialize exact
        ID/flags/hex bytes into the --replay adapter, run it with a ten-second timeout and
        check its exit status. Return parsed JSON snapshots and require one per input frame."""
        lines = [f"{tick} {frame.arbitration_id:x} {int(frame.is_extended_id)} {int(frame.is_remote_frame)} {int(frame.is_fd)} {frame.data.hex() or '-'}" for tick, frame in frames]
        process = subprocess.run([str(self.executable), "--replay"], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True, timeout=10)
        snapshots = [json.loads(line) for line in process.stdout.splitlines()]
        self.assertEqual(len(snapshots), len(frames))
        return snapshots

    def test_all_ten_virtual_signals_reach_firmware_snapshot(self):
        """Known engineering values must survive transport and the actual C conversions.

        Send known values for every PID through genuine virtual endpoints and the
        production C adapter; require updated flags, integer conversions and signed power.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        values = [
            ("pack_voltage_v", 400.0, "pack_voltage_deci_volts", 4000),
            ("pack_summed_voltage_v", 399.95, "pack_summed_voltage_deci_volts", 4000),
            ("pack_current_a", -123.4, "pack_current_deci_amps", -1234),
            ("pack_soc_pct", 80.5, "pack_state_of_charge", 81),
            ("highest_temperature_c", 60, "highest_temperature_c", 60),
            ("lowest_temperature_c", -20, "lowest_temperature_c", -20),
            ("average_temperature_c", 35, "average_temperature_c", 35),
            ("lowest_cell_voltage_v", 3.2, "lowest_cell_voltage_100uv", 32000),
            ("highest_cell_voltage_v", 4.1, "highest_cell_voltage_100uv", 41000),
            ("average_cell_voltage_v", 3.7, "average_cell_voltage_100uv", 37000),
        ]
        snapshots = self.replay_bus([(index * 50, encode_response(name, value)) for index, (name, value, _, _) in enumerate(values)])
        for snapshot, (_, _, field, expected) in zip(snapshots, values):
            self.assertTrue(snapshot["updated"])
            self.assertEqual(snapshot[field], expected)
        self.assertEqual(snapshots[-1]["average_pack_power_kw"], -49)

    def test_time_varying_current_retains_real_firmware_statistics(self):
        """A changing CAN current sequence must update C min/max/average and signed power.

        Replay 400 V and -100/0/200 A samples at increasing millisecond ticks. Assert
        production C current min/max/mean and -40/80/13 kW power statistics.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        snapshots = self.replay_bus([
            (0, encode_response("pack_voltage_v", 400)),
            (100, encode_response("pack_current_a", -100)),
            (200, encode_response("pack_current_a", 0)),
            (300, encode_response("pack_current_a", 200)),
        ])
        final = snapshots[-1]
        self.assertEqual(final["lowest_pack_current_deci_amps"], -1000)
        self.assertEqual(final["highest_pack_current_deci_amps"], 2000)
        self.assertEqual(final["average_pack_current_deci_amps"], 333)
        self.assertEqual(final["lowest_pack_power_kw"], -40)
        self.assertEqual(final["highest_pack_power_kw"], 80)
        self.assertEqual(final["average_pack_power_kw"], 13)

    def test_malformed_virtual_frames_preserve_state_and_recover(self):
        """Oversized, wrong-type and wrong-PCI traffic must be rejected without a sanitizer fault.

        Replay valid voltage, 64-byte FD, extended and segmented-PCI frames, then valid
        420 V. Assert rejection preserves 400 V state until recovery; sanitizers stay active.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        valid = encode_response("pack_voltage_v", 400)
        fd = can.Message(arbitration_id=0x7EB, is_extended_id=False, is_fd=True, data=bytes(valid.data) + bytes(56))
        extended = can.Message(arbitration_id=0x7EB, is_extended_id=True, data=valid.data)
        invalid = bytearray(valid.data)
        invalid[0] = 0x15
        bad_pci = can.Message(arbitration_id=0x7EB, is_extended_id=False, data=invalid)
        snapshots = self.replay_bus([(0, valid), (10, fd), (20, extended), (30, bad_pci), (40, encode_response("pack_voltage_v", 420))])
        self.assertEqual([snapshot["updated"] for snapshot in snapshots], [True, False, False, False, True])
        self.assertEqual([snapshot["pack_voltage_deci_volts"] for snapshot in snapshots], [4000, 4000, 4000, 4000, 4200])

    def test_json_timeline_and_fault_window_reach_real_driver(self):
        """Request a ramp over virtual CAN and verify that pause/malformed windows do not poison C stats.

        Use a current ramp with pause at 1 second and short DLC at 2 seconds. Plan
        requests via virtual CAN, then replay received bytes in C and assert clean recovery
        and the retained real-driver current/power averages.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({
            "duration": 4,
            "signals": {"pack_voltage_v": 400, "pack_current_a": {
                "interpolation": "linear", "points": [{"t": 0, "value": -100}, {"t": 3, "value": 200}],
            }},
            "faults": [
                {"start": 1, "end": 2, "type": "pause", "signal": "pack_current_a"},
                {"start": 2, "end": 2.5, "type": "short_dlc", "signal": "pack_current_a", "dlc": 3},
            ],
        })
        channel = "scenario-pipeline-" + uuid.uuid4().hex
        received_frames = []
        with can.Bus(interface="virtual", channel=channel, ignore_config=True) as server, can.Bus(interface="virtual", channel=channel, ignore_config=True) as client:
            runner = Runner(server, Responder(scenario))
            for elapsed, signal in [(0, "pack_voltage_v"), (0, "pack_current_a"), (1, "pack_current_a"), (2, "pack_current_a"), (3, "pack_current_a")]:
                client.send(make_request(signal))
                runner.step(elapsed)
                frame = client.recv(0)
                if elapsed == 1:
                    self.assertIsNone(frame)
                else:
                    self.assertIsNotNone(frame)
                    received_frames.append((elapsed * 1000, frame))
        snapshots = self.decode_frames(received_frames)
        self.assertEqual([snapshot["updated"] for snapshot in snapshots], [True, True, False, True])
        self.assertEqual(snapshots[-1]["average_pack_current_deci_amps"], 500)
        # The only voltage reply was at t=0. At t=3 it is stale, so that current
        # updates current statistics but must not create a power sample from old voltage.
        self.assertEqual(snapshots[-1]["average_pack_power_kw"], -40)
        refreshed = self.decode_frames(received_frames + [
            (3100, encode_response("pack_voltage_v", 400)),
            (3200, encode_response("pack_current_a", 200)),
        ])
        self.assertEqual(refreshed[-1]["average_pack_power_kw"], 20)


if __name__ == "__main__":
    unittest.main()
