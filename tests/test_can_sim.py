"""Verify the simulated firmware CAN contract, editable timelines and deterministic faults.

Pure encoding/scenario/responder tests compare exact bytes, raw extrema, point
boundaries and fault schedules. They exercise planning without opening transport
or relying on wall-clock sleeps; temporary files cover duplicate JSON fields."""

import math
from pathlib import Path
import tempfile
import unittest

from tools.can_sim.engine import Responder
from tools.can_sim.protocol import SIGNALS, encode_response, make_request, request_signal
from tools.can_sim.scenario import Scenario


class ProtocolTests(unittest.TestCase):
    """Check response bytes against the independent constants in Src/app_can.c."""

    def test_all_ten_response_encodings(self):
        """Require each implemented PID, service, PCI length and unit conversion.

        Compare independently specified exact eight-byte hex responses for all ten
        signal/unit examples; assert 0x7EB ID, standard/classic flags and DLC 8.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        examples = {
            "pack_voltage_v": (350.1, "0562f00d0dad0000"),
            "pack_summed_voltage_v": (350.12, "0562f01488c40000"),
            "pack_current_a": (-10.2, "0562f0157f990000"),
            "pack_soc_pct": (75.5, "0462f00f97000000"),
            "highest_temperature_c": (127, "0462f0287f000000"),
            "lowest_temperature_c": (-128, "0462f02980000000"),
            "average_temperature_c": (-1, "0462f02aff000000"),
            "lowest_cell_voltage_v": (3.0001, "0562f03275310000"),
            "highest_cell_voltage_v": (4.2, "0562f033a4100000"),
            "average_cell_voltage_v": (3.6, "0562f0348ca00000"),
        }
        self.assertEqual(set(SIGNALS), set(examples))
        for signal, (value, expected) in examples.items():
            with self.subTest(signal=signal):
                frame = encode_response(signal, value)
                self.assertEqual(frame.arbitration_id, 0x7EB)
                self.assertFalse(frame.is_extended_id)
                self.assertFalse(frame.is_fd)
                self.assertEqual(frame.dlc, 8)
                self.assertEqual(frame.data.hex(), expected)

    def test_request_round_trip_and_padding(self):
        """Create the firmware's eight-byte 0x22 requests for every known PID.

        Iterate every PID, asserting ID 0x7E3, PCI/service/PID prefix and four zero
        padding bytes; request_signal must return the original name.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        for signal, definition in SIGNALS.items():
            with self.subTest(signal=signal):
                frame = make_request(signal)
                self.assertEqual(frame.arbitration_id, 0x7E3)
                self.assertEqual(frame.data[:4], bytes([3, 0x22, definition.pid >> 8, definition.pid & 255]))
                self.assertEqual(frame.data[4:], bytes(4))
                self.assertEqual(request_signal(frame), signal)

    def test_rejects_invalid_request_shapes(self):
        """Ignore unrelated IDs, services, segmented PCI, unknown PIDs and nonclassic frames.

        Mutate ID, extended/remote/error/FD flags, service, PCI, PID and lengths.
        Recognition must return None without reading invalid payload offsets.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        variants = [
            {"arbitration_id": 0x123},
            {"is_extended_id": True},
            {"is_remote_frame": True},
            {"is_error_frame": True},
            {"is_fd": True},
            {"data": bytes([3, 0x21, 0xF0, 0x0D]) + bytes(4)},
            {"data": bytes([0x13, 0x22, 0xF0, 0x0D]) + bytes(4)},
            {"data": bytes([3, 0x22, 0xFF, 0xFF]) + bytes(4)},
            {"data": bytes([3, 0x22, 0xF0])},
            {"dlc": 3},
            {"data": bytes(9), "dlc": 9},
        ]
        for changes in variants:
            with self.subTest(changes=changes):
                frame = make_request("pack_voltage_v")
                for key, value in changes.items():
                    setattr(frame, key, bytearray(value) if key == "data" else value)
                self.assertIsNone(request_signal(frame))

    def test_encodes_boundary_values_without_wrapping(self):
        """Map legitimate extrema to exact wire values, including offset current.

        Compare extrema against explicit ffff/0000/fffe/c8 payload hex for voltage,
        current, SOC and cell voltage; valid inputs must retain their intended endpoints.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        examples = [
            ("pack_voltage_v", 6553.5, "ffff"),
            ("pack_summed_voltage_v", 655.35, "ffff"),
            ("pack_current_a", -3276.7, "0000"),
            ("pack_current_a", 3276.7, "fffe"),
            ("pack_soc_pct", 100, "c8"),
            ("average_cell_voltage_v", 6.5535, "ffff"),
        ]
        for signal, value, expected in examples:
            with self.subTest(signal=signal, value=value):
                width = SIGNALS[signal].width
                self.assertEqual(encode_response(signal, value).data[4:4 + width].hex(), expected)

    def test_rejects_nonfinite_and_out_of_range_values(self):
        """Stop invalid engineering values instead of silently clamping their input.

        Pass invalid voltage inputs plus out-of-range current and SOC. Each encoding
        must raise ValueError instead of clamping or wrapping an invalid engineering value.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        for value in [-0.1, 6553.6, math.inf, math.nan, True, "350"]:
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    encode_response("pack_voltage_v", value)
        with self.assertRaises(ValueError):
            encode_response("pack_current_a", -3276.8)
        with self.assertRaises(ValueError):
            encode_response("pack_soc_pct", 100.5)

    def test_raw_override_allows_unphysical_wire_extrema(self):
        """Expose raw 65535 current and raw 255 SOC for firmware boundary tests.

        Force raw current 65535 and SOC 255 and inspect exact bytes. Require ValueError
        for a raw SOC override of 256, which cannot fit its one-byte payload.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        self.assertEqual(encode_response("pack_current_a", 0, raw_override=65535).data[4:6], b"\xff\xff")
        self.assertEqual(encode_response("pack_soc_pct", 0, raw_override=255).data[4], 255)
        with self.assertRaises(ValueError):
            encode_response("pack_soc_pct", 0, raw_override=256)


class ScenarioTests(unittest.TestCase):
    """Check validation and time-dependent values independently of bus scheduling."""

    def test_linear_interpolation_and_endpoint_hold(self):
        """Ramp current through zero and hold the first/last values outside the points.

        Sample a -10..10 A ramp between 1 and 3 seconds at 0, 2 and 9 seconds.
        Assert first hold, midpoint zero and last hold independently of transport.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({"signals": {"pack_current_a": {
            "interpolation": "linear", "points": [{"t": 1, "value": -10}, {"t": 3, "value": 10}]
        }}})
        self.assertEqual(scenario.values_at(0)["pack_current_a"], -10)
        self.assertEqual(scenario.values_at(2)["pack_current_a"], 0)
        self.assertEqual(scenario.values_at(9)["pack_current_a"], 10)

    def test_step_changes_at_exact_point_and_preserves_other_defaults(self):
        """Apply step values at their timestamp and supply all unspecified signals.

        Step SOC from 100 to 0 percent at 2 seconds. Assert old value at 1.999, new
        value at 2, and a complete ten-signal mapping including omitted defaults.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({"signals": {"pack_soc_pct": {
            "interpolation": "step", "points": [{"t": 0, "value": 100}, {"t": 2, "value": 0}]
        }}})
        self.assertEqual(scenario.values_at(1.999)["pack_soc_pct"], 100)
        self.assertEqual(scenario.values_at(2)["pack_soc_pct"], 0)
        self.assertEqual(len(scenario.values_at(2)), 10)

    def test_rejects_ambiguous_or_invalid_scenario_configuration(self):
        """Reject typos, unsupported versions, invalid points and invalid fault parameters.

        Iterate misspelled fields, unsupported version, duration/track faults, bad
        fault types/intervals/targets and parameter bounds. Require ValueError per input.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        invalid = [
            {"signals": {"voltage": 3}},
            {"duraton": 1},
            {"version": 2},
            {"duration": 0},
            {"duration": math.inf},
            {"signals": {"pack_voltage_v": {"interpolation": "cubic", "points": [{"t": 0, "value": 1}]}}},
            {"signals": {"pack_voltage_v": {"points": []}}},
            {"signals": {"pack_voltage_v": {"points": [{"t": -1, "value": 1}]}}},
            {"signals": {"pack_voltage_v": {"points": [{"t": 0, "value": 1}, {"t": 0, "value": 2}]}}},
            {"signals": {"pack_soc_pct": 101}},
            {"faults": [{"start": 0, "end": 1, "type": "drop", "every": 0}]},
            {"faults": [{"start": 0, "end": 1, "type": "burst", "count": 1025}]},
            {"faults": [{"start": 0, "end": 1, "type": "delay", "seconds": -1}]},
            {"faults": [{"start": 1, "end": 1, "type": "pause"}]},
            {"faults": [{"start": 0, "end": 1, "type": "typo"}]},
            {"faults": [{"start": 0, "end": 1, "type": []}]},
            {"faults": [{"start": 0, "end": 1, "type": "pause", "signal": []}]},
            {"faults": [{"start": 0, "end": 1, "type": "raw", "signal": "pack_soc_pct", "value": 256}]},
            {"faults": [{"start": 0, "end": 1, "type": "wrong_id", "id": "0x800"}]},
        ]
        for configuration in invalid:
            with self.subTest(configuration=configuration):
                with self.assertRaises(ValueError):
                    Scenario.from_dict(configuration)

    def test_json_file_loading_rejects_duplicate_fields(self):
        """Read an editable scenario and reject accidental duplicate signal entries.

        Create a temporary current scenario, verify 12.3 A reads correctly, then add
        a duplicate signal key and require loading to fail. No transport is opened.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "scenario.json"
            path.write_text('{"signals":{"pack_current_a":12.3}}')
            self.assertEqual(Scenario.from_file(path).values_at(0)["pack_current_a"], 12.3)
            path.write_text('{"signals":{"pack_current_a":12.3,"pack_current_a":-12.3}}')
            with self.assertRaises(ValueError):
                Scenario.from_file(path)

    def test_checked_in_scenarios_validate_and_cover_every_fault_type(self):
        """Keep the editable examples runnable and include every documented fault control.

        Read normal.json and edge_cases.json; assert normal has no faults and the
        edge-case set matches every supported FAULT_PARAMETERS type.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        from tools.can_sim.scenario import FAULT_PARAMETERS

        directory = Path(__file__).resolve().parents[1] / "tools" / "can_sim" / "scenarios"
        normal = Scenario.from_file(directory / "normal.json")
        self.assertFalse(normal.faults)
        edge_cases = Scenario.from_file(directory / "edge_cases.json")
        self.assertEqual({fault.type for fault in edge_cases.faults}, set(FAULT_PARAMETERS))

    def test_fault_intervals_are_start_inclusive_end_exclusive(self):
        """Activate a targeted fault only within its stated interval and signal.

        Apply a current-only pause from 1 to 2 seconds. Assert active at 1, inactive
        at 2 and inactive for the untargeted voltage signal.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({"faults": [{"start": 1, "end": 2, "type": "pause", "signal": "pack_current_a"}]})
        self.assertEqual(len(scenario.active_faults(1, "pack_current_a")), 1)
        self.assertFalse(scenario.active_faults(2, "pack_current_a"))
        self.assertFalse(scenario.active_faults(1, "pack_voltage_v"))


class ResponderTests(unittest.TestCase):
    """Exercise deterministic response faults without wall-clock sleeps."""

    def responder(self, *faults, signals=None):
        """Build a one-second fault window with optionally specified signal values.

        Fault dicts are wrapped in the same start=1/end=2-second interval; optional signals
        supply engineering inputs, otherwise defaults apply. Return a new Responder with
        independent loss counters. Invalid fixture data propagates parser errors; no transport."""
        return Responder(Scenario.from_dict({
            "signals": signals or {},
            "faults": [{"start": 1, "end": 2, **fault} for fault in faults],
        }))

    def test_samples_value_at_request_time(self):
        """Encode the interpolated value when a request arrives, even if sending is delayed.

        Request the current ramp at 1 second with 0.2-second delay. Assert due 1.2
        seconds and raw bytes for the sampled 10 A, not the future delivery-time value.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        responder = self.responder({"type": "delay", "seconds": 0.2}, signals={"pack_current_a": {
            "interpolation": "linear", "points": [{"t": 0, "value": 0}, {"t": 2, "value": 20}]
        }})
        planned = responder.plan(make_request("pack_current_a"), 1)
        self.assertEqual(planned[0].due, 1.2)
        self.assertEqual(planned[0].message.data[4:6], bytes.fromhex("8063"))

    def test_pause_window_recovers_and_unrelated_requests_are_ignored(self):
        """Drop responses during pause and resume after its end without altering requests.

        Request before, during and after a 1..2-second pause, then change request ID.
        Assert one response outside the interval and no response during/for unrelated ID.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        responder = self.responder({"type": "pause"})
        request = make_request("pack_voltage_v")
        self.assertEqual(len(responder.plan(request, 0.9)), 1)
        self.assertFalse(responder.plan(request, 1.5))
        self.assertEqual(len(responder.plan(request, 2)), 1)
        request.arbitration_id = 1
        self.assertFalse(responder.plan(request, 2))

    def test_drop_every_second_matching_request(self):
        """Apply the loss cadence only to requests matching the active fault target.

        Interleave current and voltage requests under a current-only every=2 fault.
        Assert voltage does not advance the matching-current drop cadence.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        responder = self.responder({"type": "drop", "every": 2, "signal": "pack_current_a"})
        current = make_request("pack_current_a")
        self.assertEqual(len(responder.plan(current, 1.1)), 1)
        self.assertEqual(len(responder.plan(make_request("pack_voltage_v"), 1.2)), 1)
        self.assertFalse(responder.plan(current, 1.3))
        self.assertEqual(len(responder.plan(current, 1.4)), 1)

    def test_wrong_identifier_service_pci_and_unknown_pid_mutations(self):
        """Produce independently selected malformed response headers.

        Create each header mutation with explicit hex parameters and inspect the
        selected ID/byte/PID; expected malformed bytes must be generated exactly.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        cases = [
            ({"type": "wrong_id", "id": "0x123"}, "arbitration_id", 0x123),
            ({"type": "wrong_service", "service": "0x7F"}, 1, 0x7F),
            ({"type": "wrong_pci", "pci": "0x25"}, 0, 0x25),
            ({"type": "unknown_pid", "pid": "0xFFFF"}, "pid", 0xFFFF),
        ]
        for fault, field, expected in cases:
            with self.subTest(fault=fault):
                message = self.responder(fault).plan(make_request("pack_voltage_v"), 1)[0].message
                actual = message.arbitration_id if field == "arbitration_id" else (
                    int.from_bytes(message.data[2:4], "big") if field == "pid" else message.data[field]
                )
                self.assertEqual(actual, expected)

    def test_short_dlc_truncates_actual_payload(self):
        """Send an actual four-byte frame rather than just lying in its DLC field.

        Request voltage with DLC 4 fault; assert both Message.dlc and actual data
        length/header bytes reflect truncation rather than mismatched metadata.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        message = self.responder({"type": "short_dlc", "dlc": 4}).plan(make_request("pack_voltage_v"), 1)[0].message
        self.assertEqual(message.dlc, 4)
        self.assertEqual(message.data.hex(), "0562f00d")

    def test_duplicate_spacing_and_burst_count(self):
        """Schedule extra duplicates with spacing and bounded simultaneous bursts.

        Plan two extra copies spaced 0.1 seconds at t=1, then a 32-frame burst.
        Assert due times 1/1.1/1.2 and all burst frames due simultaneously at 1.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        planned = self.responder({"type": "duplicate", "count": 2, "spacing": 0.1}).plan(make_request("pack_voltage_v"), 1)
        self.assertEqual([frame.due for frame in planned], [1, 1.1, 1.2])
        burst = self.responder({"type": "burst", "count": 32}).plan(make_request("pack_voltage_v"), 1)
        self.assertEqual(len(burst), 32)
        self.assertEqual({frame.due for frame in burst}, {1})

    def test_raw_fault_reaches_current_and_soc_wire_boundaries(self):
        """Bypass engineering ranges explicitly for decoder saturation tests.

        Plan targeted raw-current 65535 and raw-SOC 255 faults. Inspect payload bytes
        without opening a bus; engineering-range bypass must be explicit.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        current = self.responder({"type": "raw", "value": 65535, "signal": "pack_current_a"})
        self.assertEqual(current.plan(make_request("pack_current_a"), 1)[0].message.data[4:6], b"\xff\xff")
        soc = self.responder({"type": "raw", "value": 255, "signal": "pack_soc_pct"})
        self.assertEqual(soc.plan(make_request("pack_soc_pct"), 1)[0].message.data[4], 255)


if __name__ == "__main__":
    unittest.main()
