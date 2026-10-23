# Portable synthetic dashboard UI evidence

## Source and decisions

- Reviewed `Src/app_tasks.c` and the generated `ui/ui.c`, main/debug screens and UI source list. The existing task file mixed FreeRTOS scheduling, console handling and transport-independent LVGL formatting. Moved only the latter helpers, auxiliary types and automatic demo provider into `Inc/app_dashboard_ui.h` / `Src/app_dashboard_ui.c` so firmware and host scenarios use the same display code.
- All eight function definitions remaining in `Src/app_tasks.c` are byte-identical to the completed documentation baseline on `codex/can-documentation-tests`; verified through Tree-sitter function extraction. Queue, CAN polling, recording, console dispatch and task scheduling code are retained. Root added the portable source to firmware CMake.
- The public auxiliary enums retain their existing names and values. Each existing `fault_active` status boolean now has a `valid` boolean. A zero-initialized host auxiliary state is explicit unknown; the automatic firmware demo marks its synthetic statuses valid/healthy and retains its speed/mode update rules. Unknown speed/mode are rendered as dashes, including the demo's initial invalid speed before its first 200 ms update. Unknown digital statuses use configured hex `0x808080` and remain visible. Known fault/healthy colors retain `0xF40D0D` / `0x52BA00`; RGB565 conversion affects the emitted 32-bit colors.
- The existing power indicator still maps the magnitude of current to its 1..100 range. Its generated `PWR kWh` caption and units remain a requirements gap, not a new vehicle encoding.
- Reproduced the eMMC percentage overflow: 67,108,352 used bytes divided by 134,217,216 total bytes was displayed as 18% through uint32 multiplication; with a uint64 intermediate it displays the expected rounded 50%. Valid logger counters remain uint32; the percentage helper now uses uint64 only for intermediate multiplication/addition.
- Reviewed local LVGL display-buffer and color-conversion APIs. `tools/dashboard_host/render.c` registers a 480×272 display with a 32-row partial buffer, copies flushed regions using the supplied area's stride into a memory framebuffer, and exports through LVGL RGB565-to-32-bit conversion to RGB PPM. It advances LVGL using an absolute caller-controlled uint32 tick. It performs no hardware, touch, wall-clock, or adapter I/O. Teardown deletes generated screens and the display; LVGL's custom-allocation configuration retains its global core initialization.

## Permanent regression checks

`tests/dashboard_ui/test_binding.c` links the production portable UI module, actual generated UI and checked-in LVGL. Only RAM/eMMC usage getters are test stubs.

Checks cover:

- Unknown speed/mode replace prior known label contents with dashes.
- All seven unknown status panels are neutral and visible after boot.
- All-known healthy statuses are hidden after boot; each of the seven independently injected faults maps to the correct red panel and exposes the status group.
- Invalid status overrides a stale fault flag with neutral color; invalid drive mode renders a neutral dash.
- Existing automatic demo state transitions at 0, 200, 3,000, 5,000 and 10,000 ms retain expected synthetic speed/mode/boot behavior.
- Signed extrema for temperature/power, millivolt rounding and maximum encoded cell voltage display correctly.
- Large eMMC usage rounds to 50% and 100%, RAM usage to 50%, and zero capacity to 0%.
- Actual framebuffer capture creates a binary P6 480×272 RGB file of 391,695 bytes; null/missing-directory paths and capture after shutdown fail.
- Tests explicitly compile with `-UNDEBUG` so their assertions remain enabled for Release configurations.

Executed:

```sh
.venv/bin/cmake -S tools/dashboard_host -B build/ui-agent -G Ninja -DCMAKE_BUILD_TYPE=Debug
.venv/bin/cmake --build build/ui-agent --target dashboard_ui_tests -j8
.venv/bin/ctest --test-dir build/ui-agent --output-on-failure
```

Result: `dashboard_ui_bindings` passed, with ASan/UBSan instrumenting the test, portable UI, renderer, LVGL and generated UI. A copied-source mutation restoring the old uint32 arithmetic failed the production label expectation; shared source remained unchanged.

Converted the test's generated PPM using macOS `sips` and inspected `build/ui-agent/ui-binding-test.png`: mode/speed dashes, the neutral BSPD panel and maximum/clamped indicator positions were visible in the actual checked-in main screen. The test capture is an edge fixture, not a proposed redesigned UI.

The portable module compiles with Arm GNU 15.3.1 and the existing firmware warning/CPU options. A stricter `-Werror -Wpedantic` probe failed on existing duplicate semicolons in generated `ui/ui.h` declarations; no new module warning was identified. The normal firmware compile succeeded while reporting those pre-existing header warnings. Root performs final Debug/Release firmware link and integrated synthetic-pipeline checks.

## Limits

These checks validate LVGL binding/rendering and synthetic state handling. They do not verify MCU peripherals, FreeRTOS scheduling, LCD electrical behavior, physical CAN, real digital polarities, data freshness deadlines, or vehicle alert thresholds. Physical input mappings remain deferred by the user.
