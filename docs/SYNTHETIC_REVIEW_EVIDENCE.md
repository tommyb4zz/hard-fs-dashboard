# Independent synthetic dashboard review

## Scope and source record

Reviewed `docs/SYNTHETIC_TEST_PLAN.md` and the scenario, UI and storage agents' `SYNTHETIC_*_EVIDENCE.md` records. Inspected `tools/dashboard_host/{main.c,fake_hal.c,render.c,CMakeLists.txt}`, `tools/dashboard_sim/{runner.py,__main__.py,viewer.html}`, auxiliary parsing in `tools/can_sim/scenario.py`, the CAN response scheduler in `tools/can_sim/engine.py`, `Inc/app_dashboard_ui.h`, `Src/app_dashboard_ui.c`, `Src/app_emmc_log.c`, `Inc/app_emmc_log.h`, `dump_emmc_bin.py` and their dedicated regression tests.

This review used the checked-in implementation and copied probes in ignored `build/synthetic-review/`. No shared implementation was edited, no physical interfaces were opened, and no firmware was flashed. Known integration include ordering and RGB565 expectation corrections were excluded from findings because the root agent was already resolving them.

## Reproduced findings before root corrections

### P2: A scenario can be overwritten by its own generated artifacts

At review time, `tools/dashboard_sim/__main__.py` read the scenario through `Scenario.from_file`, ran the simulation, then reread the source path to copy it to `scenario.json`. `run_scenario` wrote output files without checking whether a generated artifact aliased the source input.

Reproduction inside the ignored review directory:

```sh
.venv/bin/python -m tools.dashboard_sim \
  --scenario build/synthetic-review/collision/summary.json \
  --output build/synthetic-review/collision --capture-every-ms 0
```

The source was valid JSON containing a 0.2-second scenario and 400 V input. The command returned zero. The original `summary.json` input was replaced by the generated run summary, and the copied `scenario.json` contained the run summary rather than the scenario used. `build/synthetic-review/output-collision-evidence.json` records the observed bytes/comparisons.

Required correction: retain the exact input bytes before execution and reject source aliases of generated artifacts before any output writes. Include existing hard links in alias checks. Add a regression that verifies failure preserves the input, and a positive regression that the saved scenario matches the exact bytes used.

### P2: Numeric overflow bypasses the host protocol bounds

At review time, `tools/dashboard_host/main.c` used `sscanf` integer conversions before its range checks. On the reviewed macOS host, the command below was accepted with exit zero and the actual speed label became `0`:

```text
T 0 4294967296 0 127 0 0
Q
```

The same oversized integer was accepted in status-mask and metadata fields after conversion to zero. These commands are outside the declared protocol bounds. Ordinary JSON scenarios are validated before serialization, so this finding concerns the host boundary's independent validation.

Required correction: parse complete numeric tokens with checked `strto*` conversion, overflow/error checks and explicit ranges before narrowing. Add positive boundary and oversized-token regressions. `build/synthetic-review/host-boundary-probes.json` records the commands' results.

### P2: Sanitizer diagnostics can be hidden by a successful host exit

At review time, the host CMake target used default `-fsanitize=address,undefined` behavior, and `Host.__exit__` only exposed its captured stderr when the host return code was nonzero. A copied standalone signed-overflow program built with those flags emitted a UBSan runtime error, returned zero, and was accepted by `Host.command`/context cleanup as `{"ok": true}`.

This reproduction establishes a validation-gate gap; it does not identify undefined behavior in the production dashboard. The diagnostic and accepted result are stored in `build/synthetic-review/ubsan-recovery-evidence.json`.

Required correction: use `-fno-sanitize-recover=all` for instrumented host targets or an equivalent explicit fail-on-diagnostic policy. Apply the same strict behavior to test compile commands when describing those tests as a sanitizer gate.

### P3: A one-character payload is accepted as an empty frame

At review time, `read_frame` computed `strlen(hex)/2`, then checked odd length only when that resulting size was nonzero. Both `7eb 0 0 0 f` and `7eb 0 0 0 z` were accepted as empty payloads in a one-frame step. A lone invalid character should be rejected by the stated frame syntax.

Required correction: reserve `-` for an empty payload and require every other payload token to have an even, nonzero hexadecimal length before decoding. These cases are in `build/synthetic-review/host-boundary-probes.json`.

## Independent verification performed

At the reviewed state:

```text
.venv/bin/python -m unittest discover -s tests -p test_dashboard_sim.py -v
3 integration tests passed

.venv/bin/python -m unittest discover -s tests -p test_aux_scenario.py -v
17 auxiliary scenario tests passed

.venv/bin/python -m unittest discover -s tests -p test_storage_export.py -v
17 production-C/Python storage/export tests passed

.venv/bin/ctest --test-dir build/dashboard-host --output-on-failure
1 production UI binding/renderer test passed
```

A copied-source mutation changed unknown speed serialization from `-1` to `0`. The existing integration test `test_unknown_and_fault_window_recover_without_replacing_the_demo` failed at its actual LVGL speed-label expectation (`0` versus `-`). The other two cases passed. This confirms that the unknown-input check detects that regression rather than merely mirroring an implementation detail. The mutated source and results remain only under ignored `build/synthetic-review/`; shared implementation was not changed.

No additional actionable defect was identified in the reviewed auxiliary parser, partial-tail storage changes, CRC reader, framebuffer area stride, UI status mapping or viewer text handling. This statement is limited to the inspected code and executed cases, not an exhaustive correctness proof. Existing input freshness, physical decoding/polarity, bus-off policy, persistence recovery and RTOS/concurrency requirements remain outside this synthetic implementation.

## Disposition

Findings were sent to the root agent with reproductions while integration work continued. Final correction verification is recorded below after root changes are available; pre-correction passing test counts above are not the final full-suite result.

## Final correction verification

All four reproduced findings above were corrected in the implementation and independently rechecked:

- **Scenario preservation:** CLI now reads source bytes once and parses those bytes through `Scenario.from_text`. `run_scenario` checks source aliases against every scheduled artifact before creating/writing run artifacts, using resolved paths and `samefile` for existing hard links. A successful 0.2-second CLI run preserved the source and saved byte-identical `scenario.json`. Both the original `summary.json` collision and an existing hard link from `inputs.json` to `log.bin` now exit 1, preserve the source bytes, and create no `snapshots.jsonl`.
- **Numeric bounds:** host tokenization now requires exact field counts and checked `strtoll` conversion with full-token, overflow and range checks before narrowing. Oversized speed, masks, metadata and timestamp probes now exit 2. Valid maximum speed/mode/masks and explicit unknown speed/mode with an empty `-` frame still exit 0.
- **Payload syntax:** one-character `f` and `z` payload probes now exit 2; the explicit empty marker remains accepted.
- **Strict sanitizer failure:** all instrumented host/LVGL/UI targets and dedicated storage/CAN test compile commands now include `-fno-sanitize-recover=all`. The previously recoverable copied overflow probe rebuilt with those flags exits `-6` on this host, and `Host.command` rejects it with `RuntimeError`. This checks the failure policy without modifying production code.

The exact final reproductions are retained under ignored `build/synthetic-review/final/` in `boundary-results.json`, `cli-results.json` and `strict-sanitizer.json`.

Post-correction independent checks:

```text
Dashboard integration: 6 tests passed
Production-C/Python storage/export: 17 tests passed
Production CAN pipeline replay: 4 tests passed
Production UI binding/renderer CTest: 1 test passed
```

No residual actionable issue was identified within this bounded review. The hardware, concurrency and persistence limits stated earlier still apply. Root's project-wide tests and firmware build results are recorded separately.
