# Live dashboard, diagnostics and retained malfunction history

The production firmware creates the repository's LVGL screens. It no longer invokes the
synthetic auxiliary generator: speed, mode and safety-status inputs remain unknown until
a vehicle-specific provider supplies them. The existing ten-PID CAN request/reply schema
is unchanged. Its compatibility with the actual slave PCB is still unverified.

## Screens and data

- Main: existing three gauges and status indicators. Numeric captions show temperature,
  calculated instantaneous power in kW and SOC. Missing/stale gauge fills disappear and
  captions show `--`; missing speed/mode use `-`. Gauge extents/colors are presentation
  scaling, not validated alarm thresholds.
- Cells/current: the existing red HIGH/AVG/LOW table shows cell mV, signed temperature C,
  calculated lifetime power statistics kW, and lifetime current statistics A. The fourth
  row is now current; it no longer mislabels storage occupancy as speed.
- Pack/storage: pack V, summed V, SOC, RAM/eMMC log usage and availability. Missing and
  stale signals have distinct text states. All ten received measurements are represented
  across these views. Runtime freshness is per signal; `APP_CAN_STALE_MS` defaults to 2000
  ms and is a display timeout, not a vehicle safety limit.
- Faults: copied immutable events, newest first, four per page, with NEWER/OLDER controls.
  Each event carries sequence, boot/session ID, boot-relative milliseconds, source, state
  and detail. The screen abbreviates time to seconds; CSV retains milliseconds.
  Session IDs advance from the last saved event; boots with no events are not counted.

Long-press the main screen to enter diagnostics. Long-press the cell table to return to
main. PACK/CELLS/FAULTS buttons link diagnostic views; the history MAIN button returns to
main. The browser screen selector shows recorded captures, not clickable firmware pixels.
All screen objects are created once and reused; updates change values rather than layouts.

## What is recorded

Accepted CAN replies establish per-signal validity and age. Never-received and stale
signals are distinguishable from a genuine received zero. After a two-second startup
allowance, unavailable CAN/auxiliary inputs are recorded as UNKNOWN. Known auxiliary
faults record FAULT immediately. Recovery appends CLEAR and recurrence appends a new FAULT.
CAN rejected-frame and timeout counters are observed with their cumulative counts; multiple
frames between GUI refreshes are represented by the count, not invented per-frame times.
Storage unavailability, write errors and telemetry capacity exhaustion are also monitored.
Identical state/detail refreshes do not create duplicate entries. No reset/erase/overwrite
API exists for malfunction records. The legacy battery `fault_flags` field is not decoded.
No overvoltage/overtemperature or other vehicle-specific thresholds are invented.

The GUI task alone owns fault observation, history navigation and LVGL operations. CAN
acquisition publishes retained snapshots/counters even when no measurement arrives; the
GUI re-evaluates freshness every 100 ms using the same HAL clock. LVGL time accounts for
actual elapsed time, including storage delays.

## Persistence and limits

Existing telemetry uses logical blocks 4096..266239 (128 MiB, 512-byte blocks). The fault
journal uses the next 4096 blocks, 266240..270335 (2 MiB), with one checksummed, versioned,
little-endian event per block. These raw regions must be reserved for this firmware;
there is no filesystem or coexistence with arbitrary external eMMC data.

Boot scans and verifies the existing prefix before appending. Telemetry session reset does
not touch the journal. Writes wait for card transfer-ready and verify a full readback.
An identical retry is allowed, while a different occupied or corrupt slot is never
rewritten. Corruption blocks later persistent appends so evidence is preserved. Previously
valid entries remain readable. This is append-only application behavior with CRC error
detection, not cryptographic tamper protection or guaranteed survival of interrupted card
programming. Physical power-cut behavior still needs board testing.

Storage is finite: 4096 durable events. A full journal refuses new events instead of
rotating over old records, and increments the displayed `lost` count. If storage is absent,
blocked, or temporarily failing, up to 256 pending events remain in RAM; the screen reports
CHECK STORAGE and the pending count. Pending events are volatile until successfully saved.
Transient write failures retry in order; unavailable storage at boot needs a later reboot
with working storage to recover the old journal. Previously recorded durable events are
never erased by normal recovery. There is no promise of unlimited retention.

Telemetry's original 32-byte measurement prefix and 42-byte portable export remain
unchanged; runtime freshness metadata does not enlarge the RAM history or on-card records.

## Run and inspect

```sh
.venv/bin/python -m tools.dashboard_sim \
  --scenario tools/can_sim/scenarios/dashboard.json \
  --output build/dashboard-complete --capture-every-ms 500 --open
```

The browser's Screen selector offers Dashboard, Cells/current, Pack/storage and Malfunction
history. Play/pause, seek and speed controls use captures rendered by the real C/LVGL code.
`fault-history.csv` exports every retained event, oldest first. JSON input files explicitly
provide simulated values; they are not the production firmware's data source.

For persistent host restarts, set `DASHBOARD_STORAGE_FILE` to a dedicated fake-card file
when launching `build/dashboard-host/dashboard_host`. Never point it at a device or an
unrelated file. The host uses a sparse ordinary file, not real eMMC or CAN. The C command
interface supports `S main|debug|pack|faults`, `P <ppm-path>`, `H <history-csv-path>` in
addition to the existing step/export commands. Python `Host(executable, storage_file)`
provides the same optional persistence for deterministic restart tests.

## Validation

Focused CAN/UI CTest suites use AddressSanitizer and UndefinedBehaviorSanitizer. Python
integration includes actual C journal serialization, restart retention, corrupt-slot
preservation, multi-screen capture and existing CAN/storage/playback tests. UI regressions
cover independent expiry, valid zero, recovery, navigation and history pagination. Both
Debug and Release firmware must link successfully. Host results do not establish working
LCD/touch/eMMC/CAN hardware, actual bus compatibility or vehicle alarm thresholds.

### Verified on 2026-10-30

- Python unittest discovery: 106 tests passed.
- CAN driver CTest: 2/2 passed with ASan/UBSan.
- UI CTest: 1/1 passed with ASan/UBSan, plus explicit repeated-screen capture check.
- JavaScript playback: 8/8 passed (also executed by Python test discovery).
- Production-code simulation: 20 seconds, 2001 steps, 314 requests, 956 synthetic responses,
  271 validated telemetry records, 41 capture times for each of four screens.
- Debug and Release firmware linked. RAM_D1: 227048 and 227032 bytes respectively of 512 KiB.
- Browser runtime: screen selection, pause and end seek verified; no recorded console warnings/errors.
- Function inventory regenerated and `--check` passed.

Logs and captures are local under `build/`; ignored build outputs are not committed.
The installed ARM toolchain archiver must be on PATH, for example:

```sh
PATH="$PWD/build/_deps/arm-toolchain-expanded/Payload/bin:$PATH" .venv/bin/cmake --build build/Debug
PATH="$PWD/build/_deps/arm-toolchain-expanded/Payload/bin:$PATH" .venv/bin/cmake --build build/Release
```
