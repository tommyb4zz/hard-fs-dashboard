# Synthetic dashboard integration evidence

This report records the original host integration. The subsequent automatic-playback change,
100 ms default capture interval, and updated test counts are recorded in
[SYNTHETIC_PLAYBACK_EVIDENCE.md](SYNTHETIC_PLAYBACK_EVIDENCE.md).

## Sources and decisions

Reviewed the supplied BDP, current application source, local LVGL display/color APIs, and the
scenario/UI/storage agents' shared evidence. The bounded contract is in
[SYNTHETIC_TEST_PLAN.md](SYNTHETIC_TEST_PLAN.md). A host memory display was selected to execute the
checked-in UI without inventing peripheral emulation or requiring SDL. Actual C-generated
requests travel to the existing virtual responder, and its messages return to the production
decoder. Auxiliary timelines feed the shared portable UI module directly because no vehicle
encodings or GPIO polarities were supplied.

The host reuses `Src/app_can.c`, `app_can_config.c`, `app_data_log.c`, `app_emmc_log.c` and
`app_dashboard_ui.c`, the generated `ui/` and checked-in LVGL 8.4.0. Its fake boundaries and
validated stepping/capture/export commands are in `tools/dashboard_host/`. Python orchestration,
lossless framebuffer conversion and offline viewer generation are in `tools/dashboard_sim/`.
The firmware retains its automatic demo through the extracted shared UI module.

The host uses 64 fake 512-byte blocks at the existing logger start address, with one block for
the header. The current 40-byte record layout holds twelve records per data block: 756 records
fit. This is a bounded test region, not a physical capacity claim. Logging records the combined
battery snapshot once for a poll with accepted updates; a burst is not one record per frame.
Auxiliary states are retained in `snapshots.jsonl`; the unchanged binary/CSV record schema contains
battery data, not speed/mode/seven auxiliary status fields.

## Integrated verification

Executed from `/Users/youseffarahat/Documents/hard-fs-dashboard`:

```sh
.venv/bin/python -m unittest discover -s tests -p 'test_*.py'
.venv/bin/cmake -S tests/can_host -B build/can-host -DCAN_HOST_SANITIZERS=ON
.venv/bin/cmake --build build/can-host -j8
.venv/bin/ctest --test-dir build/can-host --output-on-failure
.venv/bin/ctest --test-dir build/dashboard-host --output-on-failure
.venv/bin/pre-commit run --all-files
.venv/bin/python tools/generate_function_docs.py --check
git diff --check
```

Results: **82 Python tests passed**, **both CAN CTest targets passed**, and
**the actual LVGL binding/renderer CTest passed**. Instrumented host, CAN, UI and storage builds
use ASan/UBSan and `-fno-sanitize-recover=all` so undefined-behavior diagnostics fail execution.
The Python runtime is 3.13.7, python-can 4.6.1 and CMake 4.4.4; the host compiler is Clang.
Formatting and diff checks passed. The generated inventory covers **477 function definitions**
and **365 resolved call/reference edges** with no missing function comments or purpose headers
in the audited project source types. Vendor dependencies retain upstream documentation.

The six dashboard integration tests verify custom battery/auxiliary timelines in actual labels,
decoded signed current, fault/unknown status colors after RGB565 conversion, malformed-window
rejection and recovery, real PNG captures, stored/export CRCs, bounded host commands, oversized
integer and one-character payload rejection, input preservation including hard links, and run
argument/step-count bounds. Detailed tests for all seven mappings, demo timing, storage tail
reuse, capacity and failure/retry behavior are in the separate evidence reports.

Independent review reproduced four additional defects before correction: artifact/input aliasing,
numeric conversion overflow, one-character payload acceptance and recoverable UBSan diagnostics.
All four were fixed and independently rechecked. An isolated unknown-speed mutation was rejected
by the actual-label integration test. See [SYNTHETIC_REVIEW_EVIDENCE.md](SYNTHETIC_REVIEW_EVIDENCE.md).

## Executed scenarios and browser inspection

```sh
.venv/bin/python -m tools.dashboard_sim --scenario tools/can_sim/scenarios/dashboard.json
.venv/bin/python -m tools.dashboard_sim --scenario tools/can_sim/scenarios/user-inputs.json --output build/user-inputs-demo
```

The 20-second supplied stress scenario produced **2001 steps, 314 production requests,
956 received virtual response frames, 271 exported records and 21 captures**. Its export passed
stored/dump/stream CRC validation. The simpler editable example produced **1001 steps, 188 requests,
177 responses/records and 11 captures** over ten seconds. Exact generated scenario bytes, summaries,
snapshots, binary/CSV and images are in their respective ignored output directories.

Inspected actual 480×272 framebuffer captures at 14 and 15 seconds: simultaneous statuses were
red at 14 seconds and explicitly unknown/gray at 15 seconds. The configured current-magnitude
bar saturated at the injected raw current maximum; its existing `PWR kWh` caption remains a
documented units gap.

The browser-testing skill was applied. Chrome DevTools MCP was unavailable; the available Codex
browser control was used for actual localhost verification. Served `build/dashboard-sim` at
`http://127.0.0.1:8866/`, opened the generated viewer, and operated the accessible time slider
at 14, 15 and 16 seconds. Observed matching state text, changed frame paths and successfully
loaded 480×272 images. At 16 seconds all seven healthy status panels were hidden. Captured
console warning/error list was empty. The screenshot is `build/dashboard-sim/viewer-verified.jpg`.
This checks recorded viewing, not live input editing in a browser.

## Firmware and checkout verification

Built both existing presets using Arm GNU 15.3.1 and the explicit FreeRTOS V10.6.2 override:

```sh
export PATH="$PWD/build/_deps/arm-toolchain-expanded/Payload/bin:$PWD/.venv/bin:$PATH"
.venv/bin/cmake --build --preset Debug -j8
.venv/bin/cmake --build --preset Release -j8
```

Both linked successfully. Internal FLASH usage was 56,188 bytes Debug / 55,248 bytes Release;
QSPI usage was 837,320 / 780,264 bytes respectively. The host-only code is not in firmware.
These builds use previously configured
`FREERTOS_PACK_DIR=$PWD/build/_deps/FreeRTOS-Kernel`; the configured default ST pack was absent.
Build success does not verify board operation.

HEAD and fetched `origin/develop` both resolve to
`6b09c089496336eba3a5202c30aab794786f6851`. All **1579** paths from
`git ls-tree -r --name-only origin/develop` are present in the requested checkout. Work is local
and uncommitted; no push, PR, flash or physical-interface operation was performed.

## Boundaries

The host does not establish FreeRTOS/concurrency timing, controller arbitration/bitrate/registers,
physical FIFO behavior, CAN electrical compatibility, MCU cache/DMA behavior, touch/LCD hardware,
power-loss durability, persistent recovery, actual input encodings/polarities or freshness policy.
Defined scenarios and regressions do not establish exhaustive coverage of all possible edge cases.
Remaining code and required information are in [BDP_GAP_ANALYSIS.md](BDP_GAP_ANALYSIS.md).
Physical no-damage connection safety: **Insufficient data to verify**.
