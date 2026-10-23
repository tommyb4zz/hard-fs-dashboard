"""Exercise real python-can virtual transport, scheduling, CLI and physical-interface rejection.

Use unique in-process channels for actual virtual send/receive scheduling tests.
Separate fake sysfs/iproute2 fixtures exercise the vcan safety guards; subprocess
CLI tests verify the public runnable module and physical-channel rejection."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import uuid

from tools.can_sim.engine import Responder, Runner
from tools.can_sim.protocol import SIGNALS, make_request
from tools.can_sim.scenario import Scenario
from tools.can_sim.transport import open_bus, validate_vcan


class VirtualBusTests(unittest.TestCase):
    """Verify bus behavior using isolated channels and an explicit simulation clock."""

    def setUp(self):
        """Create two genuine python-can virtual endpoints on a unique channel.

        No external arguments. Allocate a UUID channel and server/client via guarded
        open_bus; register shutdown cleanups with unittest. Each test receives isolated
        genuine virtual endpoints, preventing traffic from unrelated test instances."""
        channel = "can-sim-test-" + uuid.uuid4().hex
        self.server = open_bus("virtual", channel)
        self.client = open_bus("virtual", channel)
        self.addCleanup(self.server.shutdown)
        self.addCleanup(self.client.shutdown)

    def test_all_pids_cross_actual_virtual_bus(self):
        """Round trip ten requests and require response identity and payload.

        Round-trip the ten requests on unique in-process endpoints; verify ID/PID
        and the -12.3 A raw bytes, then accepted/sent counters both equal ten.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        runner = Runner(self.server, Responder(Scenario.from_dict({"signals": {"pack_current_a": -12.3}})))
        for index, signal in enumerate(SIGNALS):
            with self.subTest(signal=signal):
                self.client.send(make_request(signal))
                runner.step(index / 10)
                response = self.client.recv(timeout=0.2)
                self.assertIsNotNone(response)
                self.assertEqual(response.arbitration_id, 0x7EB)
                self.assertEqual(int.from_bytes(response.data[2:4], "big"), SIGNALS[signal].pid)
                if signal == "pack_current_a":
                    self.assertEqual(response.data[4:6], bytes.fromhex("7f84"))
        self.assertEqual(runner.sent, 10)
        self.assertEqual(runner.accepted, 10)

    def test_delay_and_pause_over_bus_with_deterministic_clock(self):
        """Withhold responses before due time and recover from request loss afterward.

        Advance explicit seconds around 0.2 delay and 1..2 pause boundaries. Assert
        no early frame, due delivery, pause loss and immediate post-pause response.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({"faults": [
            {"start": 0, "end": 1, "type": "delay", "seconds": 0.2},
            {"start": 1, "end": 2, "type": "pause"},
        ]})
        runner = Runner(self.server, Responder(scenario))
        self.client.send(make_request("pack_voltage_v"))
        runner.step(0)
        self.assertIsNone(self.client.recv(timeout=0))
        runner.step(0.199)
        self.assertIsNone(self.client.recv(timeout=0))
        runner.step(0.2)
        self.assertIsNotNone(self.client.recv(timeout=0.1))
        self.client.send(make_request("pack_voltage_v"))
        runner.step(1)
        self.assertIsNone(self.client.recv(timeout=0))
        self.client.send(make_request("pack_voltage_v"))
        runner.step(2)
        self.assertIsNotNone(self.client.recv(timeout=0.1))

    def test_duplicate_and_malformed_frames_cross_bus(self):
        """Preserve intentional short DLC and duplicate count over the bus boundary.

        Combine DLC 3 with two extra copies. Require three actually received short
        frames and no fourth frame, proving transport preserves intentional malformed data.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({"faults": [
            {"start": 0, "end": 1, "type": "short_dlc", "dlc": 3},
            {"start": 0, "end": 1, "type": "duplicate", "count": 2},
        ]})
        runner = Runner(self.server, Responder(scenario))
        self.client.send(make_request("pack_voltage_v"))
        runner.step(0)
        responses = [self.client.recv(timeout=0.1) for _ in range(3)]
        self.assertTrue(all(frame is not None and frame.dlc == 3 and len(frame.data) == 3 for frame in responses))
        self.assertIsNone(self.client.recv(timeout=0))

    def test_pending_queue_limit_stops_unbounded_delayed_bursts(self):
        """Fail explicitly before scheduling more messages than the configured queue limit.

        Plan four delayed frames into a runner capped at three. Require OverflowError
        and an empty queue because the request batch is rejected before heap insertion.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({"faults": [
            {"start": 0, "end": 1, "type": "delay", "seconds": 1},
            {"start": 0, "end": 1, "type": "burst", "count": 4},
        ]})
        runner = Runner(self.server, Responder(scenario), max_pending=3)
        self.client.send(make_request("pack_voltage_v"))
        with self.assertRaises(OverflowError):
            runner.step(0)
        self.assertEqual(runner.pending, 0)

    def test_rejects_clock_reversal(self):
        """Prevent a scheduler from silently accepting time travel between steps.

        Step at 2 seconds then at 1. Require ValueError rather than silently moving
        the scheduler clock backward; no requests are necessary.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        runner = Runner(self.server, Responder(Scenario.from_dict({})))
        runner.step(2)
        with self.assertRaises(ValueError):
            runner.step(1)


class InterfaceSafetyTests(unittest.TestCase):
    """Reject physical interfaces even when their names look like virtual interfaces."""

    def sysfs(self, root, channel="vcan0", virtual=True, link_type="280"):
        """Build the sysfs evidence fixture used before any SocketCAN bus opens.

        Root is a temporary directory; channel/virtual/link_type select fixture evidence.
        Create device directories, type text and class/net symlink, returning the root Path.
        This writes only a fake tree and never modifies the operating system /sys."""
        base = Path(root)
        actual = base / "devices" / ("virtual" if virtual else "pci0000:00") / "net" / channel
        actual.mkdir(parents=True)
        (actual / "type").write_text(link_type)
        net = base / "class" / "net"
        net.mkdir(parents=True)
        (net / channel).symlink_to(actual, target_is_directory=True)
        return base

    def link_result(self, kind="vcan"):
        """Return the kernel link-kind response normally supplied by iproute2.

        Kind selects the mocked info_kind string; return CompletedProcess with successful
        JSON stdout for vcan0. No subprocess is launched by this helper; validate_vcan tests
        patch subprocess.run to inject this result."""
        return subprocess.CompletedProcess([], 0, stdout=json.dumps([{"ifname": "vcan0", "linkinfo": {"info_kind": kind}}]))

    def test_accepts_only_virtual_can_sysfs_and_vcan_kernel_kind(self):
        """Require both the virtual CAN sysfs path and the explicit vcan link kind.

        Build a virtual sysfs fixture and mock iproute2 reporting info_kind vcan.
        Validation must return without exception; no actual SocketCAN socket is opened.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        with tempfile.TemporaryDirectory() as temporary:
            root = self.sysfs(temporary)
            with patch("tools.can_sim.transport.subprocess.run", return_value=self.link_result()):
                validate_vcan("vcan0", sysfs_root=root, platform="linux")

    def test_rejects_physical_device_named_vcan0(self):
        """Reject a hardware CAN network interface regardless of its misleading name.

        Build class/net/vcan0 pointing into a simulated PCI device tree. Require
        ValueError despite its virtual-looking name, without touching a hardware device.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        with tempfile.TemporaryDirectory() as temporary:
            root = self.sysfs(temporary, virtual=False)
            with self.assertRaises(ValueError):
                validate_vcan("vcan0", sysfs_root=root, platform="linux")

    def test_rejects_noncan_virtual_device_and_vxcan(self):
        """Do not infer vcan from a name, virtual sysfs path or CAN device type alone.

        Reject sysfs type 1 and, separately, kernel kind vxcan with CAN type 280.
        Independent evidence must agree rather than relying on a name alone.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        with tempfile.TemporaryDirectory() as temporary:
            root = self.sysfs(temporary, link_type="1")
            with self.assertRaises(ValueError):
                validate_vcan("vcan0", sysfs_root=root, platform="linux")
        with tempfile.TemporaryDirectory() as temporary:
            root = self.sysfs(temporary)
            with patch("tools.can_sim.transport.subprocess.run", return_value=self.link_result("vxcan")):
                with self.assertRaises(ValueError):
                    validate_vcan("vcan0", sysfs_root=root, platform="linux")

    def test_rejects_missing_kernel_evidence_and_nonlinux(self):
        """Fail closed when sysfs, iproute2 or Linux SocketCAN support is unavailable.

        Check missing sysfs, mocked missing ip executable and platform darwin.
        Require ValueError in each case before any SocketCAN endpoint could open.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError):
                validate_vcan("vcan0", sysfs_root=Path(temporary), platform="linux")
            root = self.sysfs(temporary)
            with patch("tools.can_sim.transport.subprocess.run", side_effect=FileNotFoundError):
                with self.assertRaises(ValueError):
                    validate_vcan("vcan0", sysfs_root=root, platform="linux")
            with self.assertRaises(ValueError):
                validate_vcan("vcan0", sysfs_root=root, platform="darwin")

    def test_disallows_other_backends_and_physical_channel_names(self):
        """Block backend selection and channel names that can address physical adapters.

        Call open_bus for pcan and for socketcan channel can0. Both must raise
        ValueError before adapter selection or a physical network bind.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        with self.assertRaises(ValueError):
            open_bus("pcan", "PCAN_USBBUS1")
        with self.assertRaises(ValueError):
            open_bus("socketcan", "can0")


class CLITests(unittest.TestCase):
    """Check the public runnable module without opening any external CAN interface."""

    def test_default_virtual_demo_requests_and_receives(self):
        """Run the in-process demo and verify JSON frame events and a final summary.

        Launch the standalone virtual --demo CLI for 0.11 simulated seconds at rate 5.
        Parse stdout JSON lines; require successful exit, an RX event and positive sent count.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        result = subprocess.run(
            [sys.executable, "-m", "tools.can_sim", "--demo", "--duration", "0.11", "--rate", "5"],
            capture_output=True, text=True, timeout=5,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        records = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertTrue(any(record["event"] == "demo_rx" for record in records))
        self.assertEqual(records[-1]["event"], "summary")
        self.assertGreater(records[-1]["sent"], 0)

    def test_cli_rejects_physical_bus_before_open(self):
        """Reject physical SocketCAN channels without transmitting or attempting a bind.

        Launch the CLI requesting socketcan/can0. Require nonzero exit and vcan error
        text; the configured guard rejects the physical channel before opening a bus.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        result = subprocess.run(
            [sys.executable, "-m", "tools.can_sim", "--interface", "socketcan", "--channel", "can0", "--duration", "0.1"],
            capture_output=True, text=True, timeout=5,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("vcan", result.stderr)


if __name__ == "__main__":
    unittest.main()
