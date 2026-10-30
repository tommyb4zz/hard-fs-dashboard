# Dashboard completion contract

## Objective and scope
Keep the repository's 480x272 LVGL gauge layout and red high/average/low debug table.
Runtime measurements must come from accepted CAN responses; absent/stale signals show
unknown rather than a plausible zero. Firmware auxiliary inputs remain unknown until a
real provider exists; no invented CAN IDs or alarm limits. Host scenarios explicitly
supply synthetic inputs for testing.

## Capabilities and order
1. signal-state: ten-signal validity/timestamps, 2000 ms configurable display freshness,
   malformed-response/request-timeout counters; preserve existing wire conversions.
2. diagnostics: correct units/captions, battery/current/pack detail pages, storage and
   communication health, shared real input bindings and safe screen navigation.
3. fault-history: append-only raised/cleared/unavailable events with sequence, session,
   uptime timestamp, source and detail; immutable prior records, no clear/overwrite API.
   Include missing/stale inputs, malformed CAN, timeouts, explicit auxiliary faults,
   and storage errors; do not infer unprovided vehicle thresholds.
4. runtime: firmware plus sanitizer host integration, all-screen captures and CSV export.

## Storage
A finite journal must never silently overwrite old records. Report full, corrupt or
unavailable storage and pending/omitted events explicitly. User confirmed power-cycle retention. A separate 4096-slot eMMC journal follows the
existing telemetry reservation; live recovery never clears recorded history. Existing telemetry export framing must remain readable.

## Implementation and verification
C11, existing LVGL 8 code/vendor sources unchanged, no new dependencies.
Use production C through HAL/BSP host fakes, focused C regressions and Python integration.
Commands:
- .venv/bin/cmake --build build/can-host
- .venv/bin/ctest --test-dir build/can-host --output-on-failure
- .venv/bin/cmake --build build/dashboard-host
- .venv/bin/ctest --test-dir build/dashboard-host --output-on-failure
- .venv/bin/python -m unittest discover -s tests -p 'test_*.py'
- .venv/bin/cmake --build build/Debug
- .venv/bin/cmake --build build/Release
- .venv/bin/python tools/generate_function_docs.py
- .venv/bin/clang-format --dry-run --Werror <changed C/H sources>

## Acceptance
No production demo values, independent freshness incl rollover and legitimate zero;
old fault records unchanged after recovery/reoccurrence; pagination and navigation
work for empty/populated history; all runtime views use the actual LVGL renderer.
Build Debug/Release firmware and verify host paths with sanitizers. Hardware execution,
actual slave protocol, electrical behavior and power-loss durability require a real board.
