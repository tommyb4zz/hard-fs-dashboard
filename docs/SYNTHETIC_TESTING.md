# Editable synthetic dashboard

The implementation is in this repository. It runs the production CAN request scheduler and decoder,
RAM/eMMC loggers, portable UI bindings, checked-in LVGL 8.4.0 and generated screens on the host.
Battery responses travel over an actual python-can virtual bus. Speed, drive mode and seven status
inputs use a separate explicit synthetic provider; no CAN encoding is invented for those inputs.
The existing automatic firmware demo remains in `Src/app_dashboard_ui.c` and is still called by the
firmware GUI task.

## Run with your inputs

From the repository root:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/can_sim/requirements.txt -r tools/requirements-dev.txt
.venv/bin/python -m tools.dashboard_sim --scenario tools/can_sim/scenarios/user-inputs.json --open
```

CMake and a host Clang/GCC compiler with address/undefined-behavior sanitizer support are required.
The command builds the host target automatically. Edit
[user-inputs.json](../tools/can_sim/scenarios/user-inputs.json) and rerun. The default output directory
is `build/dashboard-sim`; use `--output build/my-test` to retain another run.

The playback regression tests also require Node.js (verified locally with 22.18.0); the viewer
itself needs only a browser, and the generator does not require Node.

`--open` opens `build/dashboard-sim/index.html` in your default browser after generation. The viewer
automatically plays and loops actual rendered frames, synchronized with their decoded values and
supplied input states. It has play/pause, replay, speed, loop and time controls. Reduced-motion
preference starts it paused; press Play to animate. The slider also lets you inspect a particular
time. This is recorded playback of the deterministic run. Edit the scenario JSON and rerun to
change inputs; playback does not rerun CAN processing or edit vehicle input mappings.

Outputs are `scenario.json`, `summary.json`, `snapshots.jsonl`, `log.bin`, `log.csv`, PNG captures and
the HTML viewer. `log.bin` is produced by the production C exporter against fake eMMC and validated
by the Python reader, including stored-record, dump-record and stream CRCs. To decode it separately:

```sh
.venv/bin/python dump_emmc_bin.py --input-bin build/dashboard-sim/log.bin --output-prefix build/redecoded
```

Binary/CSV records retain the existing battery schema. Speed, mode and auxiliary status inputs
are recorded in `snapshots.jsonl`. Each updated poll logs one combined snapshot, including when
several accepted frames arrive in the same batch.

## Input definitions

All `t` values are elapsed seconds. Constants stay fixed. A `points` array defaults to step
interpolation; `"interpolation": "linear"` interpolates numeric inputs. Before/after the supplied
timeline, the first/last point is retained. Duplicate times and unsupported values are rejected.

| Group | Inputs | Meaning in this synthetic contract |
|---|---|---|
| `signals` | `pack_voltage_v`, `pack_summed_voltage_v` | Volts, encoded using the existing ten-PID source protocol |
| `signals` | `pack_current_a`, `pack_soc_pct` | Signed amperes and percentage |
| `signals` | `highest_temperature_c`, `lowest_temperature_c`, `average_temperature_c` | Celsius |
| `signals` | `lowest_cell_voltage_v`, `highest_cell_voltage_v`, `average_cell_voltage_v` | Volts |
| `auxiliary` | `speed_kph` | 0..65535 or `null`; numeric step/linear timelines. UI rounds to whole km/h. `null` renders a dash; null points require step interpolation. |
| `auxiliary` | `drive_mode` | `"N"`, `"D"`, `"R"` or `null`; constant or step timeline. Null renders a dash. |
| `auxiliary.statuses` | `imd`, `fault`, `rtd`, `tsal`, `bspd`, `sdc`, `ready_to_drive` | `"unknown"`, `"ok"` or `"fault"`; constants or step timelines. These are display states, not specified vehicle polarities. |

Omitted auxiliary inputs are unknown. After the existing three-second boot display, all seven
status panels are hidden only when every status is known healthy. If any status is unknown or
faulted, all seven remain visible: healthy is green, fault is red, and unknown is gray. Colors
in snapshots reflect conversion through the configured RGB565 format.

For battery signal ranges, raw encoding and timed traffic faults, see
[CAN_SIMULATION.md](CAN_SIMULATION.md). Battery fields currently start at zero and retain their last
accepted value during a pause. They do **not** yet have per-signal unknown/stale metadata. A request
timeout is not a measurement freshness rule.

## Faults and timing

[dashboard.json](../tools/can_sim/scenarios/dashboard.json) is the supplied 20-second stress scenario:
ramps and regenerative current, N/D/R/unknown mode, each status fault, simultaneous faults, unknown
states, recovery, pause/drop/delay, wrong IDs/services/PCI, short DLC, unknown PID, duplicates, bursts,
and raw current/SOC values. Run it with:

```sh
.venv/bin/python -m tools.dashboard_sim --scenario tools/can_sim/scenarios/dashboard.json --open
```

The default deterministic step is 10 ms and capture interval is 100 ms (10 frames/s at that step).
Set `--capture-every-ms 50` for 20 frames/s, or `1000` for a smaller one-frame-per-second recording.
Finer captures create more image files. Playback uses the saved timestamps and never invents
intermediate images. `--step-ms` accepts 1..1000;
`--capture-every-ms 0` disables images; `--duration` overrides the scenario duration. Requests are
generated by C and their replies enter C on a subsequent step. Delays are quantized to these steps.
There are at most 100001 steps. The fake storage has 64 blocks, including one header block; with
the current 40-byte record layout, 756 records fit. An append failure stops the run explicitly.
This capacity is a test limit, not the physical card's capacity.

## Code locations and verification boundary

| Path | Responsibility |
|---|---|
| `tools/can_sim/scenario.py` | Strict editable battery, auxiliary and fault timelines |
| `tools/can_sim/engine.py` | Responses and timed fault injection |
| `tools/dashboard_sim/runner.py` | Closed-loop virtual CAN, explicit clock, artifacts and export validation |
| `tools/dashboard_sim/playback.js`, `viewer.html` | Timestamp-based playback and synchronized actual image/input-state display |
| `tools/dashboard_host/main.c` | Validated host commands; production CAN/UI/logging stepping |
| `tools/dashboard_host/fake_hal.c` | Memory-only CAN FIFO, clock, MMC, UART sink and single-thread mutex boundary |
| `tools/dashboard_host/render.c` | Actual 480×272 LVGL memory display and framebuffer export |
| `Src/app_dashboard_ui.c` | Shared screen bindings and retained firmware auto demo |
| `tests/test_dashboard_sim.py` | User input, rendered labels, malformed-window recovery and export integration |
| `tests/dashboard_ui/test_binding.c` | All seven status mappings, unknown states, usage arithmetic and actual rendering |
| `tests/test_storage_export.py` | Production logger/export regressions and failure/retry controls |

The host does not execute firmware startup or FreeRTOS tasks. It does not emulate controller
registers, arbitration/bitrate, electrical CAN faults, actual FIFO capacity, touch or LCD hardware,
DMA/cache coherence, simultaneous task execution or power loss. No physical adapter or serial
device is opened. Existing board firmware separately uses normal transmit-capable CAN mode.
The host is not proof of physical connection safety.

The provided scenarios exercise defined cases; exhaustive coverage of every possible edge case is
not established. Remaining requirements and the supplied BDP findings are in
[BDP_GAP_ANALYSIS.md](BDP_GAP_ANALYSIS.md). Shared source, decisions and verification evidence are in
the [scenario](SYNTHETIC_SCENARIO_EVIDENCE.md), [UI](SYNTHETIC_UI_EVIDENCE.md),
[storage](SYNTHETIC_STORAGE_EVIDENCE.md) and [integration](SYNTHETIC_INTEGRATION_EVIDENCE.md) reports.
