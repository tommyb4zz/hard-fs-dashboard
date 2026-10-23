# Independent virtual simulation and function graph review

Reviewed against `docs/IMPLEMENTATION_PLAN.md` and the virtual-only clarification. Physical adapter, BMS and DBC information is deferred; this review does not establish physical integration or electrical safety.

## Result

No required correctness findings remain in the reviewed simulator, production-driver replay pipeline or function inventory generator. One required factual documentation correction was identified and resolved: the linked ST DS12556 Rev 8 assigns PH13/PH14 AF9 in **Table 8**, printed page 88, rather than Table 9. Both `docs/VIRTUAL_ENVIRONMENTS.md` and `docs/RESEARCH.md` now cite Table 8.

## Scope and source checks

Tests were inspected before implementation. Reviewed `tests/test_can_sim.py`, `tests/test_can_sim_bus.py`, `tests/test_can_pipeline.py` and `tests/test_function_docs.py`; then all `tools/can_sim/*.py`, checked-in scenarios, `tools/generate_function_docs.py`, `README.md`, `docs/FUNCTION_FLOW.md`, `docs/CAN_SIMULATION.md`, `docs/VIRTUAL_ENVIRONMENTS.md` and generated inventory data.

- Scenario parsing rejects unknown fields, duplicate JSON keys, unsupported versions, nonfinite/out-of-range values, invalid intervals and duplicate or decreasing point times. Step and linear tracks hold endpoints; fault windows include their start and exclude their end. Unspecified signals receive the documented synthetic zero values.
- Responder encoding matches the ten production PID formats, including big-endian payloads, signed temperatures, offset current and the documented engineering scales. Delayed responses sample at request handling time; overlapping fault ordering, targeted drop counters, duplicates, bursts, truncation and pause recovery match the documented behavior.
- Dispatch rejects backward clocks, bounds each request drain and ready-frame batch, and rejects pending queue overflow before adding the new response batch. Send failures propagate. The CLI describes its best-effort host clock and skipped missed demo request periods; it does not claim physical arbitration or hard real-time timing.
- `tools/can_sim/transport.py:14` requires Linux, a `vcan` name, virtual-network sysfs location, CAN interface type, absence of a hardware device and kernel link kind `vcan`. `open_bus` accepts only the in-process virtual backend or this verified vcan path and disables external CAN configuration. Tests reject physical interfaces, physical interfaces renamed to vcan, vxcan, non-CAN interfaces, unavailable verification metadata and other hardware backends. Linux vcan selection was tested with fixtures; kernel vcan traffic was not executed on this macOS host.
- `tests/test_can_pipeline.py:41` sends and receives actual python-can virtual messages before replaying the **received** payload and metadata. The adapter compiles the production C driver under AddressSanitizer and UndefinedBehaviorSanitizer. The tests verify all ten snapshot fields, current/power statistics, a JSON current ramp, pause and malformed response windows, state retention, recovery and a real 64-byte FD payload. This is host execution of production decoding behind a fake HAL boundary, not MCU peripheral or full firmware task emulation.
- Tree-sitter parses C definitions and calls rather than commented text; Python AST collection assigns nested-body calls to their own function. Resolution preserves file-local linkage and excludes test fakes from production-source targets. Regression tests cover both fixes. Conditional source variants and legacy scripts are explicitly included without evaluating build selection; external and ambiguous symbols remain unresolved, and callback references do not assert runtime execution.
- A fresh in-memory inventory equals `docs/functions.json` exactly: **346 definitions**, **295 resolved call/reference edges**, zero function-comment gaps and zero purpose-header gaps. Its language counts are 243 C, 98 Python, two assembly, two shell and one CMake definition. No production `Src/` function resolves an edge to `tests/`. The manual flowchart identifies documented processing order, task handoffs and callback relationships separately from a measured trace.

## Executed verification

From the worktree root:

```sh
.venv/bin/python -m unittest discover -s tests -p 'test_*.py' -v
.venv/bin/python tools/generate_function_docs.py --output build/function-documentation-review --check
```

The Python command passed **41 tests**: 19 protocol/scenario/responder tests, 12 virtual-bus/interface/CLI tests, four production-C pipeline tests and six inventory parser/resolution tests. Pipeline compilation uses `-Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined`; no sanitizer failure occurred. The generator check exited zero and reported 346 definitions and 295 edges. Output was directed to ignored `build/` so shared documentation was not regenerated during review.

## Isolated mutation experiment

Copied the simulator Python package, the simulator unit test and its two scenario JSON files into `build/can-simulation-review-mutation/` (nine shared inputs). Changed only the copied `scenario.py` timestamp validation from `timestamp < 0 or (parsed and timestamp <= parsed[-1][0])` to `timestamp < 0`, permitting duplicate timestamps.

Ran the copied suite using the worktree virtual-environment Python with the copied directory as its working directory:

```sh
/Users/youseffarahat/.codex/worktrees/can-documentation-tests/hard-fs-dashboard/.venv/bin/python -m unittest discover -s tests -p test_can_sim.py -v
```

The experiment exited **1**: 19 tests ran, and the duplicate-timestamp case in `ScenarioTests.test_rejects_ambiguous_or_invalid_scenario_configuration` failed because `ValueError` was no longer raised. The detailed log is `build/can-simulation-review-mutation/mutation-test.log`. SHA-256 comparisons confirmed all nine shared input files were unchanged. Production code and shared tests were not edited.

## Independent primary-source verification

Repository claims were checked against their own documentation rather than inferring hardware support:

| Source | Verified claim |
|---|---|
| [ST DS12556 Rev 8](https://www.st.com/resource/en/datasheet/stm32h750xb.pdf) | Table 8, printed page 88 maps PH13 to FDCAN1_TX and PH14 to FDCAN1_RX under AF9. The review corrected the erroneous Table 9 citation. |
| [ST UM2488 Rev 10](https://www.st.com/resource/en/user_manual/um2488-discovery-kits-with-stm32h745xi-and-stm32h750xb-mcus-stmicroelectronics.pdf) | Table 17, printed page 26 maps PH13/D16 to FDCAN1_TX and PH14/B17 to FDCAN1_RX; the kit target is STM32H750XBH6. |
| [python-can repository](https://github.com/hardbyte/python-can), [official virtual interface documentation](https://python-can.readthedocs.io/en/stable/interfaces/virtual.html) | Version 4.6 requires Python 3.9 or newer; virtual channels connect instances within the same Python process. This is the implemented traffic backend. |
| [Linux can-utils](https://github.com/linux-can/can-utils) | candump observes/logs, cansend sends one frame, canplayer replays logs and cangen generates traffic. Documented optional tools were not executed here. |
| [cantools](https://github.com/cantools/cantools) | Supports CAN database encoding/decoding and C message pack/unpack generation. No project DBC or tested integration is claimed. |
| [LVGL PC simulator v8.3 branch README](https://raw.githubusercontent.com/lvgl/lv_port_pc_eclipse/release/v8.3/README.md) | The host simulator uses SDL for graphics and input. Local `lvgl/lvgl.h` defines version 8.4.0; integration is explicitly a candidate, unbuilt and unverified. |
| [Linux kernel vcan documentation](https://www.kernel.org/doc/html/latest/networking/can.html#the-virtual-can-driver-vcan) | Local virtual interfaces transmit CAN frames without real controller hardware. Actual Linux traffic remains unverified on this host. |
| [Official QEMU STM32 documentation](https://www.qemu.org/docs/master/system/arm/stm32.html) | The documented machines cover STM32 F1/F2/F4/L4 and list CAN as missing. This source does not establish the required H750B-DK peripheral coverage. |
| [Renode repository](https://github.com/renode/renode) | Insufficient data to verify complete STM32H750B-DK and required peripheral support from the reviewed repository page; the documentation makes no implemented emulator claim. |

The reviewed runtime and tests substantiate the virtual-only workflow. They do not substantiate eventual BMS protocol compatibility, wiring, termination or electrical safety.
