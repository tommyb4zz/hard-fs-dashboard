# Synthetic auxiliary scenario implementation and evidence

## Sources and decision

The contract is `docs/SYNTHETIC_TEST_PLAN.md`. Existing battery formats and CAN faults were read from `tools/can_sim/scenario.py`, `tools/can_sim/protocol.py`, and `tests/test_can_sim.py`. The portable UI names were checked in `Inc/app_dashboard_ui.h`: neutral/drive/reverse and IMD, fault, RTD, TSAL, BSPD, SDC, ready-to-drive.

Auxiliary values are supplied directly to the host test UI. No CAN identifiers, GPIO polarity, status relationships or vehicle encoding were assigned. Such information is not established by this synthetic contract. Battery values continue through the existing CAN simulator and production decoder.

`Track` supplies the same endpoint hold, timestamp boundary and linear interpolation rules for battery and auxiliary values. Categorical and null inputs are restricted to step interpolation by parsing. The existing three-argument `Scenario(duration, tracks, faults)` constructor remains supported through a fresh default auxiliary mapping.

## Implemented API

`Scenario.auxiliary_at(elapsed)` returns a fresh dictionary:

```python
{
    "speed_kph": 42.5,  # float, or None when explicitly unknown
    "drive_mode": "D",  # N, D, R, or None when explicitly unknown
    "statuses": {
        "imd": "unknown",
        "fault": "ok",
        "rtd": "ok",
        "tsal": "fault",
        "bspd": "ok",
        "sdc": "ok",
        "ready_to_drive": "ok",
    },
}
```

Missing auxiliary speed/mode inputs are `None`; every omitted status is `unknown`. Battery defaults retain their existing synthetic zero behavior. Elapsed time must be finite, numerical and nonnegative. Samples before the first point or after the last point hold the nearest endpoint.

The optional JSON `auxiliary` object accepts `speed_kph`, `drive_mode`, and `statuses`. Status keys are exactly the seven names above. Each input is a constant or an object with optional `interpolation` (default `step`) and a nonempty `points` array of `{"t": seconds, "value": input}`.

```json
{
  "auxiliary": {
    "speed_kph": {
      "interpolation": "linear",
      "points": [{"t": 0, "value": 0}, {"t": 5, "value": 80}]
    },
    "drive_mode": {
      "points": [{"t": 0, "value": "N"}, {"t": 1, "value": "D"}]
    },
    "statuses": {
      "imd": {
        "points": [{"t": 0, "value": "unknown"}, {"t": 1, "value": "ok"}]
      },
      "sdc": "ok"
    }
  }
}
```

Speed accepts finite numbers within 0..65535 or explicit null; this is the synthetic test range, not a verified vehicle range. Numerical speed may use linear interpolation. Null speed, drive modes and statuses require step interpolation. Every point requires both time and an explicit value, including explicit null for unknown speed/mode. Point times must be finite, nonnegative and strictly increasing. Unknown fields, duplicate JSON keys at every nesting level, booleans, invalid categories, nonfinite numbers and out-of-range speed values are rejected.

## Editable example

`tools/can_sim/scenarios/dashboard.json` defines ten battery signals, a 20-second speed ramp, N/D/R/null drive changes, every status changing between unknown/ok/fault, and existing timed CAN faults. The status sequence is explicitly supplied independently of battery communication:

| Time | Auxiliary status inputs | Battery response fault |
| --- | --- | --- |
| 0 | All unknown | None |
| 0.5 | All ok | None |
| 2..3 | All unknown | Pause |
| 3 | All ok | Pause ended |
| 4+i..5+i, i=0..6 | Status i fault, then ok | Other editable fault windows |
| 14..15 | All fault | Raw current override |
| 15..16 | All unknown | Raw SOC override |
| 16 | All ok | Raw override ended |

Status i follows the order IMD, fault, RTD, TSAL, BSPD, SDC, ready-to-drive. Drive mode becomes null at 17 seconds and returns to D at 18. The example also includes drop, delay, wrong ID, wrong service, wrong PCI, short DLC, unknown PID, duplicate and burst windows. No fault window automatically sets an auxiliary status; the example's correlated changes are explicit test inputs.

## Verification

Tests were written before implementation. The first run of the 17 new test methods failed with 15 errors because the auxiliary API/schema and dashboard example did not exist. Validation-only tests already rejected the unsupported auxiliary field at that point.

After implementation:

```text
.venv/bin/python -m unittest discover -s tests -p test_aux_scenario.py -v
Ran 17 tests: OK

.venv/bin/python -m unittest discover -s tests -p 'test_can*.py' -v
Ran 35 tests: OK
```

The new tests cover constants, exact speed bounds, numerical ramps, endpoint holds, null step/recovery, all drive categories, all seven status categories, malformed objects/times/values, duplicate JSON keys, independent sample dictionaries, direct constructor compatibility, unchanged battery sampling/fault intervals, and the checked-in dashboard example. The existing CAN test run includes four actual production-C decoder replay tests over python-can virtual transport.

This evidence establishes parsing and deterministic scenario sampling. Host screen rendering and complete logging/export integration are separate root/UI/storage tasks in the plan. No physical interface was used.
