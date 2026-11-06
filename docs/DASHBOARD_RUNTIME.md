# Live dashboard, diagnostics and retained malfunction history

The production firmware creates the repository's SquareLine screens and adds two diagnostic
screens at runtime. It does not run the synthetic auxiliary generator: speed, drive mode and the
seven status inputs stay unknown until a vehicle-specific provider supplies them. The existing
ten-PID CAN request/reply schema is unchanged; its compatibility with the actual slave PCB is
still unverified.

## Screens and data

- **Main** (SquareLine `ui_MainScreen`): three gauges and status indicators. Captions show
  temperature, calculated instantaneous power in kW and SOC. Missing or stale gauges hide
  their fill and show `--`; missing speed and mode show `-`. Gauge extents and colors are
  presentation scaling, not alarm thresholds. The exported indicator caption "BSPB" is
  corrected to "BSPD" at runtime.
- **Cells** (SquareLine `ui_DebugScreen`): the original DebugFrame table image and labels,
  unchanged. Rows: Battery C.Volt (high/avg/low cell mV), Battery Temp (high/avg/low C),
  "Battery kWh" (since-boot high/avg/low pack power, which is in kW as the original firmware
  bound it; the caption is baked into the SquareLine image) and SPEED (since-boot
  maximum/mean/minimum km/h; `--` while speed is unknown, which is always on the current
  firmware).
- **Pack** (runtime, `Src/app_diagnostics_ui.c`): pack voltage, summed voltage, signed pack
  current and SOC, each with LIVE, STALE (received, then older than `APP_CAN_STALE_MS`, 2000
  ms) or MISSING (never received).
- **Faults** (runtime): the malfunction history newest first, four entries per page with
  NEWER/OLDER. Each entry shows its sequence number, boot ID, time since boot, FAULT /
  CLEAR / UNKNOWN and a readable description ("BMS link: replies stopped", "Pack voltage:
  stopped updating", "Storage: write errors"...). The header says whether new entries are
  saved to the card; status lines show event counts, unsaved/lost counts, CAN
  timeout/bad-frame totals and RAM/eMMC telemetry usage.

All ten received measurements appear on these screens. Screen objects are created once and
updated in place. The runtime screens live in application code, not in `ui/`, so a
SquareLine re-export cannot delete them, and every binding skips missing objects rather than
reaching LVGL's NULL assertion.

**Navigation.** Long-press the main screen to open the cell table (the SquareLine gesture).
The table's empty top-left header cell holds PACK and FAULTS buttons. Long-pressing the cell
table, pack or faults screen returns to main; the faults screen also has a MAIN button.

## What is recorded

The fault monitor (`Src/app_fault_monitor.c`) turns live inputs into **episodes**: one entry
when a problem starts (or changes kind) and one CLEAR once it has stayed away for
`APP_FAULT_CLEAR_HOLD_MS` (5 s). Repeats, counter increments and flapping inside an episode
add nothing. A healthy start adds nothing.

| Source | Recorded when | Notes |
| --- | --- | --- |
| BMS link | no signal fresh for 2 s after it was up, or no reply 10 s after boot (`APP_FAULT_STARTUP_GRACE_MS`) | a whole outage is one entry, not ten |
| Each of the ten CAN signals | never received, or stopped while the others keep updating, once the link has been up 2 s; out of a configured limit | not evaluated while the link is down |
| CAN request timeouts | timeouts continue for 3 s while the link stays up | ends after 60 s without one (`APP_FAULT_ERROR_QUIET_MS`); CLEAR holds the count |
| CAN rejected frames | first malformed reply | same 60 s quiet rule and count |
| Telemetry storage | card not ready, write failing, or an error counted between refreshes | same quiet rule |
| Telemetry storage full | RAM/eMMC telemetry overflow | |
| History storage | entries are not being saved (no card, write retrying, inconsistent journal, full) | |
| IMD, FAULT, RTD, TSAL, BSPD, SDC, Ready to drive, speed, mode | **only inputs whose provider marks them wired**: fault at once, no signal after the startup grace | unwired inputs are never recorded |

Limits are off by default: the dashboard never invents thresholds. To record out-of-range
battery values, pass the pack/BMS specification's limits at startup:

```c
app_fault_limit_t limits[APP_CAN_SIGNAL_COUNT] = {0};
limits[APP_CAN_SIGNAL_CELL_HIGH] = (app_fault_limit_t){true, minimum_100uv, maximum_100uv};
app_fault_monitor_set_limits(limits);
```

The legacy battery `fault_flags` field is still not decoded, because its definition has not
been supplied (see `docs/BDP_GAP_ANALYSIS.md`).

The GUI task alone owns fault observation, history paging and LVGL. CAN acquisition publishes
retained snapshots and counters even when no measurement arrives; the GUI re-evaluates every
100 ms on the same HAL clock.

## Persistence and limits

Telemetry uses logical blocks 4096..266239 (128 MiB). The fault journal uses the next 2^20
blocks (512 MiB, clamped to the card), one checksummed, versioned, little-endian event per
512-byte block. These raw regions must be reserved for this firmware.

At boot the end of the journal is found by binary search, about 21 block reads for any length.
The last record is checked: a torn final write (an unreadable last slot after a valid one) is
skipped and never rewritten; any other inconsistency keeps the journal as found and stops new
writes. An I/O error leaves the journal detached for that boot. Writes wait for the card,
verify a full readback, never touch an occupied slot and allow an identical retry. Boot IDs are
the telemetry session ID, advanced past the last saved boot.

Nothing can edit or erase an entry. A full journal refuses new writes; that boot's new events
are still listed from RAM (up to 256, shown as "not saved") and further ones are counted as
"lost". With episode recording a healthy boot adds no entries and a dead BMS adds one, so
filling a million entries is not a normal-use concern. This is CRC error detection, not
cryptographic tamper protection, and power-cut behavior still needs board testing.

Telemetry's original 32-byte measurement prefix and 42-byte portable export are unchanged.

## Run and inspect

Interactive, with the production C/LVGL code stepping in real time against the virtual BMS
(clicks are taps, half-second holds are long presses):

```sh
.venv/bin/python -m tools.dashboard_sim --live --scenario tools/can_sim/scenarios/dashboard.json --open
```

`--port` changes the port (default 8765, loopback only) and `--storage-file` keeps a fake card
between runs. Recorded instead, with a screen selector and CSV export:

```sh
.venv/bin/python -m tools.dashboard_sim \
  --scenario tools/can_sim/scenarios/dashboard.json \
  --output build/dashboard-complete --capture-every-ms 500 --open
```

A scenario's auxiliary inputs are wired only when the scenario defines them; omitted inputs
behave like the current firmware. On the board, console command `j` streams the whole history
as CSV (sequence, boot, uptime_ms, source, state, detail, description).

The host executable also accepts `I x y pressed` (simulated touch), `S main|debug|pack|faults`,
`P <ppm-path>` and `H <history-csv-path>`; `T` takes an optional seventh field, a wired mask
(bits 0..6 statuses, 7 speed, 8 mode; omitted means all wired). `DASHBOARD_STORAGE_FILE` selects a dedicated fake-card file; never
point it at a device or an unrelated file.

## Validation

Focused C suites run under AddressSanitizer and UndefinedBehaviorSanitizer: the ledger
(binary-search attach, torn and foreign slots, retry, full journal), the episode monitor (15
minutes of silent BMS, outages, missing and stopped PIDs, flapping inputs, counters, storage,
limits, descriptions) and the UI binding (layout, freshness, paging, real touch input, missing
runtime screens). Python integration drives the real host through restarts, a torn card slot,
a ten-minute silent BMS, several boots without providers, touch navigation and the live HTTP
server. Host results do not establish working LCD, touch, eMMC or CAN hardware, actual bus
compatibility or vehicle alarm thresholds.

### Verified on 2026-10-30

- Python unittest discovery: 133 tests passed (includes the 8 JavaScript playback tests).
- Live simulator checked in a browser: real mouse clicks and a long press navigate main,
  cell table, pack and history (including paging) through the firmware's own handlers.
- CAN driver CTest: 2/2 passed; UI CTest: 1/1 passed, plus the repeated-capture regression.
- Silent BMS for 15 simulated minutes: 1 history entry (previously 4096 entries after 7.5
  minutes, after which a real IMD fault was lost). A later IMD fault is recorded.
- Healthy BMS, unwired inputs, three 30 s boots: 0 entries (previously 9 per boot).
- `cmake --preset Debug` and `Release` configure from clean directories without PATH or
  FreeRTOS overrides and link with 0 warnings; RAM_D1 227808 / 227792 bytes of 512 KiB.
- `tools/generate_function_docs.py --check` passes; changed `Src/` files pass the CI
  clang-format check.
