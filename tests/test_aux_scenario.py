"""Verify editable synthetic speed, drive mode and seven status timelines without vehicle encodings.

Pure parser/timeline tests cover unknown inputs, speed/mode/status constants and
steps/ramps, strict JSON validation and checked-in examples. Temporary files are
used only for JSON loading; no CAN endpoint or C build is required."""

import json
import math
from pathlib import Path
import tempfile
import unittest

from tools.can_sim.scenario import Scenario


STATUS_NAMES = ("imd", "fault", "rtd", "tsal", "bspd", "sdc", "ready_to_drive")


class AuxiliaryScenarioTests(unittest.TestCase):
    """Check auxiliary values, explicit unknown states and strict JSON validation."""

    def scenario(self, auxiliary):
        """Parse one auxiliary object while leaving existing battery defaults intact.

        Input auxiliary is a JSON-like speed/mode/status object. Return Scenario.from_dict
        with that object and default battery tracks; validation errors propagate. No file/bus I/O."""
        return Scenario.from_dict({"auxiliary": auxiliary})

    def test_missing_auxiliary_is_explicitly_unknown(self):
        """Default only the auxiliary inputs to unknown; retain ten battery defaults.

        Parse an empty scenario at t=0 seconds; require null speed/mode, seven unknown
        statuses, and ten battery values equal to synthetic zero.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({})
        self.assertEqual(scenario.auxiliary_at(0), {
            "speed_kph": None,
            "drive_mode": None,
            "statuses": dict.fromkeys(STATUS_NAMES, "unknown"),
        })
        self.assertEqual(len(scenario.values_at(0)), 10)
        self.assertTrue(all(value == 0 for value in scenario.values_at(0).values()))

    def test_constants_and_independent_statuses(self):
        """Keep synthetic status meanings separate instead of deriving one status from another.

        Sample at 999 seconds with speed 87.25 km/h, mode D and mixed status constants.
        Assert no status is inferred from another and omitted names stay unknown.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = self.scenario({
            "speed_kph": 87.25,
            "drive_mode": "D",
            "statuses": {"imd": "ok", "fault": "fault", "ready_to_drive": "ok"},
        })
        state = scenario.auxiliary_at(999)
        self.assertEqual(state["speed_kph"], 87.25)
        self.assertEqual(state["drive_mode"], "D")
        self.assertEqual(state["statuses"], {
            "imd": "ok", "fault": "fault", "rtd": "unknown", "tsal": "unknown",
            "bspd": "unknown", "sdc": "unknown", "ready_to_drive": "ok",
        })

    def test_speed_linear_ramp_and_endpoint_hold(self):
        """Interpolate numerical speed and hold the first and last supplied values.

        Supply 0..100 km/h points at 1..3 seconds. Assert before/after endpoint holds
        and exact quarter/midpoint linear values at 1.5 and 2 seconds.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = self.scenario({"speed_kph": {
            "interpolation": "linear", "points": [{"t": 1, "value": 0}, {"t": 3, "value": 100}],
        }})
        for elapsed, expected in [(0, 0), (1, 0), (1.5, 25), (2, 50), (3, 100), (99, 100)]:
            with self.subTest(elapsed=elapsed):
                self.assertEqual(scenario.auxiliary_at(elapsed)["speed_kph"], expected)

    def test_step_speed_can_move_between_known_and_unknown(self):
        """Apply null speed at the exact step timestamp and recover on a later step.

        Use speed 10, null and 20 km/h at 0, 1 and 2 seconds. Assert values immediately
        before each boundary and at the boundary; null must remain explicit.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = self.scenario({"speed_kph": {"points": [
            {"t": 0, "value": 10}, {"t": 1, "value": None}, {"t": 2, "value": 20},
        ]}})
        for elapsed, expected in [(0.999, 10), (1, None), (1.999, None), (2, 20)]:
            with self.subTest(elapsed=elapsed):
                self.assertEqual(scenario.auxiliary_at(elapsed)["speed_kph"], expected)

    def test_drive_mode_steps_include_unknown_and_all_supported_modes(self):
        """Sample N, D, R and null at exact boundaries without numerical interpolation.

        Use null/N/D/R points at whole seconds; sample their exact timestamps and a
        later endpoint. Assert categorical values are held rather than interpolated.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = self.scenario({"drive_mode": {"points": [
            {"t": 0, "value": None}, {"t": 1, "value": "N"},
            {"t": 2, "value": "D"}, {"t": 3, "value": "R"},
        ]}})
        for elapsed, expected in [(0, None), (1, "N"), (2, "D"), (3, "R"), (9, "R")]:
            with self.subTest(elapsed=elapsed):
                self.assertEqual(scenario.auxiliary_at(elapsed)["drive_mode"], expected)

    def test_every_status_steps_through_all_states(self):
        """Check all seven status names at unknown, ok, fault and recovery boundaries.

        Reuse an unknown/ok/fault/ok track for all seven statuses. Assert each name
        at exact steps and just before transitions, covering fault recovery.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        timeline = {"interpolation": "step", "points": [
            {"t": 0, "value": "unknown"}, {"t": 1, "value": "ok"},
            {"t": 2, "value": "fault"}, {"t": 3, "value": "ok"},
        ]}
        scenario = self.scenario({"statuses": dict.fromkeys(STATUS_NAMES, timeline)})
        for elapsed, expected in [(0, "unknown"), (0.999, "unknown"), (1, "ok"),
                                  (1.999, "ok"), (2, "fault"), (3, "ok")]:
            with self.subTest(elapsed=elapsed):
                self.assertEqual(scenario.auxiliary_at(elapsed)["statuses"], dict.fromkeys(STATUS_NAMES, expected))

    def test_constant_speed_boundaries(self):
        """Accept the documented synthetic speed bounds and explicit unknown value.

        Parse speed 0, 65535, 0.25 km/h and null in separate subtests. Assert each
        is accepted and returned unchanged before host integer rounding.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        for value in [0, 65535, 0.25, None]:
            with self.subTest(value=value):
                self.assertEqual(self.scenario({"speed_kph": value}).auxiliary_at(0)["speed_kph"], value)

    def test_rejects_invalid_constant_types_values_and_unknown_fields(self):
        """Reject typos, booleans, nonfinite numbers and unsupported mode/status values.

        Feed unsupported object shapes, misspelled names, nonfinite/oversized speed,
        booleans and invalid mode/status constants; require ValueError for every case.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        invalid = [None, [], {"speed": 1}, {"speed_kph": -0.01}, {"speed_kph": 65535.01},
                   {"speed_kph": True}, {"speed_kph": "12"}, {"speed_kph": math.nan},
                   {"speed_kph": math.inf}, {"speed_kph": 10 ** 1000},
                   {"drive_mode": "P"}, {"drive_mode": "d"}, {"drive_mode": 0},
                   {"drive_mode": True}, {"drive_mode": []},
                   {"statuses": None}, {"statuses": []}, {"statuses": {"IMD": "ok"}},
                   {"statuses": {"imd": None}}, {"statuses": {"imd": True}},
                   {"statuses": {"imd": 0}}, {"statuses": {"imd": "healthy"}},
                   {"statuses": {"imd": []}}]
        for auxiliary in invalid:
            with self.subTest(auxiliary=auxiliary):
                with self.assertRaises(ValueError):
                    self.scenario(auxiliary)

    def test_rejects_invalid_track_shapes_times_and_missing_values(self):
        """Require complete, ordered, finite point objects and supported interpolation.

        Feed empty/malformed points, missing fields, unknown fields, bad modes and
        negative/nonfinite/duplicate/reversed seconds; each must raise ValueError.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        invalid_tracks = [
            {}, {"points": []}, {"points": {}}, {"points": [None]},
            {"points": [{"t": 0}]}, {"points": [{"value": 1}]},
            {"points": [{"t": 0, "value": 1, "extra": 1}]},
            {"points": [{"t": -1, "value": 1}]},
            {"points": [{"t": math.inf, "value": 1}]},
            {"points": [{"t": math.nan, "value": 1}]},
            {"points": [{"t": True, "value": 1}]},
            {"points": [{"t": "0", "value": 1}]},
            {"points": [{"t": 1, "value": 1}, {"t": 1, "value": 2}]},
            {"points": [{"t": 2, "value": 1}, {"t": 1, "value": 2}]},
            {"points": [{"t": 0, "value": -1}]},
            {"points": [{"t": 0, "value": True}]},
            {"interpolation": "cubic", "points": [{"t": 0, "value": 1}]},
            {"interpolation": [], "points": [{"t": 0, "value": 1}]},
            {"extra": 1, "points": [{"t": 0, "value": 1}]},
        ]
        for track in invalid_tracks:
            with self.subTest(track=track):
                with self.assertRaises(ValueError):
                    self.scenario({"speed_kph": track})

    def test_rejects_linear_unknown_speed_or_categorical_tracks(self):
        """Permit linear interpolation only when every speed point is numerical.

        Try null-containing linear speed and linear mode/status tracks. Assert
        ValueError rather than attempting arithmetic on unknown or categorical values.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        invalid = [
            {"speed_kph": {"interpolation": "linear", "points": [{"t": 0, "value": None}]}},
            {"speed_kph": {"interpolation": "linear", "points": [
                {"t": 0, "value": 1}, {"t": 1, "value": None},
            ]}},
            {"drive_mode": {"interpolation": "linear", "points": [{"t": 0, "value": "D"}]}},
            {"statuses": {"imd": {"interpolation": "linear", "points": [{"t": 0, "value": "ok"}]}}},
        ]
        for auxiliary in invalid:
            with self.subTest(auxiliary=auxiliary):
                with self.assertRaises(ValueError):
                    self.scenario(auxiliary)

    def test_invalid_categorical_point_values_are_rejected(self):
        """Apply mode and status validation to points as well as constants.

        Place invalid mode/status values or a missing status value inside point
        objects; require the same ValueError validation as constant inputs.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        for auxiliary in [
            {"drive_mode": {"points": [{"t": 0, "value": "P"}]}},
            {"drive_mode": {"points": [{"t": 0, "value": 1}]}},
            {"statuses": {"imd": {"points": [{"t": 0, "value": None}]}}},
            {"statuses": {"imd": {"points": [{"t": 0, "value": "healthy"}]}}},
            {"statuses": {"imd": {"points": [{"t": 0}]}}},
        ]:
            with self.subTest(auxiliary=auxiliary):
                with self.assertRaises(ValueError):
                    self.scenario(auxiliary)

    def test_json_duplicate_fields_are_rejected_at_every_auxiliary_level(self):
        """Detect duplicate auxiliary, input, status and point fields while loading JSON.

        Write temporary JSON with repeated auxiliary, speed, status and point keys.
        Loading each file must raise ValueError; the temporary directory is cleaned up.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        invalid_json = [
            '{"auxiliary":{},"auxiliary":{}}',
            '{"auxiliary":{"speed_kph":1,"speed_kph":2}}',
            '{"auxiliary":{"statuses":{"imd":"ok","imd":"fault"}}}',
            '{"auxiliary":{"speed_kph":{"points":[{"t":0,"value":1,"value":2}]}}}',
        ]
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "scenario.json"
            for configuration in invalid_json:
                with self.subTest(configuration=configuration):
                    path.write_text(configuration, encoding="utf-8")
                    with self.assertRaises(ValueError):
                        Scenario.from_file(path)

    def test_rejects_invalid_sampling_time(self):
        """Reject negative, nonfinite and nonnumerical elapsed times before sampling.

        Sample auxiliary state using negative seconds, infinity, NaN, bool, string and
        None. Assert rejection before any track is evaluated.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = self.scenario({})
        for elapsed in [-1, math.inf, math.nan, True, "1", None]:
            with self.subTest(elapsed=elapsed):
                with self.assertRaises(ValueError):
                    scenario.auxiliary_at(elapsed)

    def test_returned_status_mapping_does_not_mutate_the_scenario(self):
        """Return an independent status dictionary for each sample.

        Change the imd entry in one sampled output dict. Resampling must still return
        the original ok track, proving output mappings are independent.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = self.scenario({"statuses": {"imd": "ok"}})
        scenario.auxiliary_at(0)["statuses"]["imd"] = "fault"
        self.assertEqual(scenario.auxiliary_at(0)["statuses"]["imd"], "ok")

    def test_direct_three_argument_construction_remains_compatible(self):
        """Preserve the previously available duration/tracks/faults constructor.

        Construct Scenario from parsed duration/tracks/faults without auxiliary tracks.
        Assert the default factory supplies all seven unknown statuses.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        parsed = Scenario.from_dict({})
        scenario = Scenario(parsed.duration, parsed.tracks, parsed.faults)
        self.assertEqual(scenario.auxiliary_at(0)["statuses"], dict.fromkeys(STATUS_NAMES, "unknown"))

    def test_existing_battery_sampling_and_faults_are_unchanged(self):
        """Allow synthetic auxiliary state alongside the existing battery API and pause faults.

        Combine a -10..10 A ramp, 1..2-second pause and speed 20 km/h. Assert current
        at zero crossing, start-inclusive/end-exclusive fault activation and auxiliary value.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        scenario = Scenario.from_dict({
            "signals": {"pack_current_a": {"interpolation": "linear", "points": [
                {"t": 0, "value": -10}, {"t": 2, "value": 10},
            ]}},
            "faults": [{"start": 1, "end": 2, "type": "pause"}],
            "auxiliary": {"speed_kph": 20},
        })
        self.assertEqual(scenario.values_at(1)["pack_current_a"], 0)
        self.assertEqual(len(scenario.active_faults(1, "pack_current_a")), 1)
        self.assertFalse(scenario.active_faults(2, "pack_current_a"))
        self.assertEqual(scenario.auxiliary_at(1)["speed_kph"], 20)

    def test_dashboard_example_has_speed_modes_status_faults_and_can_recovery(self):
        """Keep the editable dashboard example valid and cover all status states plus CAN faults.

        Load the checked-in dashboard JSON and compare its names with parsed tracks.
        Sample its speed/modes/statuses and 2..3-second fault recovery; no bus is opened.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        path = Path(__file__).resolve().parents[1] / "tools/can_sim/scenarios/dashboard.json"
        scenario = Scenario.from_file(path)
        with path.open(encoding="utf-8") as handle:
            configuration = json.load(handle)
        self.assertEqual(set(configuration["signals"]), set(scenario.tracks))
        self.assertEqual(set(configuration["auxiliary"]["statuses"]), set(STATUS_NAMES))
        self.assertEqual(scenario.auxiliary_at(0)["speed_kph"], 0)
        self.assertGreater(scenario.auxiliary_at(4)["speed_kph"], 0)
        modes = {scenario.auxiliary_at(second)["drive_mode"] for second in range(21)}
        self.assertEqual(modes, {None, "N", "D", "R"})
        for status in STATUS_NAMES:
            states = {scenario.auxiliary_at(second / 2)["statuses"][status] for second in range(41)}
            self.assertEqual(states, {"unknown", "ok", "fault"})
        self.assertTrue(scenario.active_faults(2, "pack_voltage_v"))
        self.assertEqual(scenario.auxiliary_at(2)["statuses"], dict.fromkeys(STATUS_NAMES, "unknown"))
        self.assertFalse(scenario.active_faults(3, "pack_voltage_v"))
        self.assertEqual(scenario.auxiliary_at(3)["statuses"], dict.fromkeys(STATUS_NAMES, "ok"))


if __name__ == "__main__":
    unittest.main()
