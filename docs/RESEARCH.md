# Shared implementation evidence

This document records source-backed findings, implementation decisions and verification results. Each agent owns one section; the root agent reviews the combined record.

## Current checkout: requested project directory

On 2026-10-10, the user requested that the completed code and remote development files reside directly in `/Users/youseffarahat/Documents/hard-fs-dashboard`. This directory is now on `develop`, with HEAD and upstream `origin/develop` both at `6b09c089496336eba3a5202c30aab794786f6851`. The completed comments, CAN fixes, simulation, tests and graphs are applied as local working-directory changes. The implementation checkpoint remains recorded on `codex/can-documentation-tests` at `9821133`.

- Before the correction, this checkout was at `2ab1426` and lacked 34 remote-development files. An independent post-migration audit verified all 1,579 remote-development paths and all 1,611 completed-branch paths exist here. All completed-branch contents matched, except the two intentionally preserved existing local edits: `.DS_Store` and `.vscode/c_cpp_properties.json`. Those edits also matched their pre-migration SHA-256 backups. This section and the current-checkout note in the implementation plan were added after that content comparison.
- The previous local development history is preserved by `codex/develop-before-sync-20261010`. Existing local edits were also saved in Git stash `d2657e55614359d1807f2d2a3d6dbad8895b6669` before being restored. The unrelated histories were not merged.
- Recreated `.venv` in this directory with Python 3.13.7 and the pinned requirements. Moved the isolated Arm/FreeRTOS build dependencies into this directory's ignored `build/_deps`; prior verification artifacts are retained under `build/prior-verification`. The independent checkout audit is retained as `build/checkout-audit.md`.
- Verification executed from this directory: all 41 Python tests passed; both host C sanitizer targets passed; the function audit reported 346 definitions and 295 edges with zero documentation gaps; Debug and Release firmware configured and linked successfully with the previously recorded Arm toolchain and explicit FreeRTOS V10.6.2 override. No hardware was flashed or used.

The sections below record the earlier implementation and review evidence; commands containing the former worktree path describe those historical runs.

## Startup simplification after user review

- Inspected the user's pasted startup source, current `Src/main.c`, the declaration in `Inc/main.h`, and the checked-in vendor MPU type/constants and `HAL_MPU_ConfigRegion` implementation. The vendor function reads its configuration argument without modifying it. This supports setting the eight shared MPU attributes once while retaining four explicit region-number/address/size configurations.
- Removed 24 repeated MPU assignments, duplicated explanation comments on forward declarations, and the redundant local `Error_Handler` declaration already provided by `Inc/main.h`. Function explanations and the script-purpose header remain. `Src/main.c` decreased from 252 to 217 lines. All five other function definitions are byte-for-byte unchanged from the saved pre-refactor source.
- `build/main-refactor/verify.py` compiles the exact baseline and current MPU functions with the actual vendor structure/macros under AddressSanitizer/UndefinedBehaviorSanitizer. The complete traces match: MPU disable, four ordered configurations containing all eleven fields, and MPU enable. A copied address mutation produces a different trace. Evidence is `build/main-refactor/equivalence.json`; this is a host boundary comparison, not hardware execution.
- Independent [startup review](MAIN_REFACTOR_REVIEW.md) reports no required findings. It separately compared function bodies and symbolic field values, rather than relying only on the compiled trace parser.
- After the refactor, all 41 Python tests, both C sanitizer targets, the documentation audit and formatting checks passed. Debug and Release firmware linked successfully; logs are `build/main-refactor/debug-build.log` and `release-build.log`. Updated FLASH usage: Debug 55,804 bytes; Release 54,880 bytes. Earlier build-size records below refer to the pre-refactor runs. No firmware was flashed.
- Regenerated the function inventory and explorer to reflect current `Src/main.c` line numbers; 346 definitions and 295 call/reference edges remain, with zero documentation gaps.

## Root: baseline and integration

- Fetched `origin/develop`; its tip was `6b09c08`. `git rev-list --left-right --count HEAD...origin/develop` reported 3 local / 5 remote commits; `git diff HEAD...origin/develop` reported no merge base. A managed worktree from the remote tip preserves the original checkout.
- Board evidence: `README.md`, `.settings/ide.store.json`, `CMakePresets.json`.
- CAN protocol evidence: `Src/app_can.c`, `Src/app_can_config.c`. This is the implemented contract; connected hardware compatibility remains unverified.

- Implemented a source-derived inventory using Tree-sitter C and Python AST, plus shell/CMake/assembly entry extraction. Final `tools/generate_function_docs.py --check` reports **346 documented project functions and 295 resolved call/reference edges**, with zero missing function explanations or source-purpose headers. Definitions under conditional compilation are retained; preprocessing, indirect-call dispatch and whole-board runtime behavior are not inferred. Upstream `Drivers/` and `lvgl/` implementation functions are excluded explicitly.
- Added `docs/FUNCTIONS.md`, machine-readable `functions.json`, grouped Mermaid `functions.mmd`, an offline searchable `function-explorer.html`, and the source-backed startup/task/CAN/UI/testing flowcharts in `FUNCTION_FLOW.md`. Browser verification exercised filtering and function selection; the final page displayed the updated inventory and source locations with no captured console errors or warnings.
- Regression tests reproduced two incorrect graph relationships before correction: calls inside nested Python functions were attributed to the enclosing function, and production HAL calls were resolved to host-test fake implementations. The generator now limits direct-call extraction to each lexical function body and prevents production/tool functions from resolving to test-only definitions. Six inventory tests pass.
- Added four virtual-bus-to-production-C integration tests in `tests/test_can_pipeline.py`. They receive actual python-can messages, replay their payload/metadata/timestamps into the production decoder under AddressSanitizer/UndefinedBehaviorSanitizer, and check all ten conversions, signed current/power statistics, malformed-frame rejection/recovery and an editable JSON ramp across pause/short-DLC windows. The replay adapter is test-only and performs no physical I/O.
- Independent simulation/graph review in [SIMULATION_REVIEW.md](SIMULATION_REVIEW.md) reports no remaining required findings. It verified exact inventory equality, source-backed repository claims and the virtual-only boundary. A copied-source mutation removing duplicate/decreasing timeline rejection caused the expected test failure; SHA-256 checks verified the shared inputs remained unchanged. The review's datasheet table-number correction is incorporated below.
- Final Python verification: `.venv/bin/python -m unittest discover -s tests -v` passed **41 tests**. Final C verification: `.venv/bin/cmake --build build/can-host-review` and `.venv/bin/ctest --test-dir build/can-host-review --output-on-failure` passed **both targets**. The direct C suite reported **15 groups, 69,431 assertions, zero failures**.
- Comment-only verification: `python3 /tmp/verify_dashboard_documentation.py` reported 64 files, 191 original definitions, 104 declarations and zero failures. The independent generator checks all current first-party definitions, including added CAN/tool/test functions. `.venv/bin/pre-commit run --all-files` and `git diff --check` passed.
- Firmware build dependencies were initially absent. Used the official [Arm GNU toolchain 15.3.rel1 macOS Arm64 package](https://gitlab.arm.com/api/v4/projects/tooling%2Fgnu-toolchains-for-arm/packages/generic/gnu-toolchain/15.3.rel1/arm-gnu-toolchain-15.3.rel1-darwin-arm64-arm-none-eabi.pkg), SHA-256 `5fb58dc3bd6684e70a02bd1c654a6b91f044cda241eb3ae663a7b9170491d7b2`, extracted under ignored `build/_deps/`. Compiler identification: GNU 15.3.1. Cloned the official [FreeRTOS-Kernel V10.6.2](https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V10.6.2), commit `18ed8886fe3eed9dbe433a82cdc62ce5dc0315eb`, under the same ignored dependency directory. CMake received an explicit `FREERTOS_PACK_DIR` override because the repository's default ST FreeRTOS pack 2.0.0 was unavailable. These results do not verify that absent pack.
- Debug and Release firmware configured and linked successfully using the above explicit dependencies. Debug: FLASH 55,836 bytes, QSPI 837,320 bytes, RAM_D1 221,576 bytes. Release: FLASH 54,896 bytes, QSPI 780,264 bytes, RAM_D1 221,568 bytes. No firmware was flashed or executed on hardware. Reproduction with those local dependencies:

  ```sh
  export PATH="$PWD/build/_deps/arm-toolchain-expanded/Payload/bin:$PWD/.venv/bin:$PATH"
  .venv/bin/cmake --preset Debug --fresh -DFREERTOS_PACK_DIR="$PWD/build/_deps/FreeRTOS-Kernel"
  .venv/bin/cmake --build --preset Debug -j8
  .venv/bin/cmake --preset Release --fresh -DFREERTOS_PACK_DIR="$PWD/build/_deps/FreeRTOS-Kernel"
  .venv/bin/cmake --build --preset Release -j8
  ```
- Scope disposition: the independently identified existing eMMC serial-export CRC discrepancy is recorded below; this task's behavior changes are confined to the CAN receive boundary and virtual-test tooling. No successful eMMC serial-export result is claimed. Physical CAN connection and electrical safety remain unimplemented/unverified, following the user's clarification.

## Documentation agent: source comments

- Scope: 64 existing project-owned sources/headers/configuration scripts receive file-purpose headers. Added semantic comments for 191 function definitions: 176 C definitions (including conditional libc/system helpers and the legacy unbuilt Screen1), 10 Python functions, 2 assembly entries, 2 shell helpers and 1 CMake helper. Added 104 function-declaration comments, including retained declarations without active project implementations.
- Sources inspected: `Src/app_tasks.c`, `Src/app_data_log.c`, `Src/app_emmc_log.c`, `Src/main.c`, interrupt/MSP/system/libc sources, both LVGL board ports, all `ui/` function-bearing sources/headers, component configuration headers, `dump_emmc_bin.py`, `.vscode/flash_board.sh`, root/UI CMake and `cmake/` scripts, startup assembly and linker layout. Generated image/font tables receive purpose headers without table changes. Upstream `Drivers/` and `lvgl/` contents remain outside this documentation slice.
- Comments describe observed arguments, return values, null-input behavior, locking, buffer/storage side effects, periodic scheduling, callbacks and fatal-error behavior. The auxiliary UI comments explicitly identify speed/mode/digital-status placeholders; no claim of CAN/GPIO inputs for those placeholders is made.
- Corrected source-backed conflicting comments: `SystemCoreClockUpdate` divides by PLLP (previous formula said PLLR); `SystemCoreClock` describes the CM7 core clock rather than HCLK; the configured HAL HSE constant is 25 MHz (previous comment mentioned 60 MHz); startup calls constructors then application main directly, and its vector section is linked at internal FLASH origin `0x08000000`.
- Verification: `python3 /tmp/verify_dashboard_documentation.py` reported 64 files, 191 definitions, 104 declarations and zero failures. It compares C/assembly/linker lexical tokens after comment removal, Python AST and noncomment tokens, and shell/CMake noncomment lines against Git HEAD. All equal the baseline. Linker mixed line endings were preserved. A separate function-comment audit found all 176 C definitions immediately preceded by explanatory comments. `sh -n .vscode/flash_board.sh` passed; `git diff --check -- <the 64 manifest paths>` passed. The checker and manifest are available to the root at `/tmp/verify_dashboard_documentation.py` and `/tmp/dashboard_documentation_manifest.json` for integration into reproducible tooling.
- Existing implementation findings: `ui/screens/ui_Screen1.c` is absent from `ui/CMakeLists.txt` and duplicates the `ui_Image1` name used by the active debug screen; its comments label it legacy/unbuilt. Retained `EXTI2_IRQHandler` and `QUADSPI_IRQHandler` declarations have commented-out project C implementations and startup default aliases. Retained `HAL_TIM_MspPostInit` and optional `assert_failed` declarations have no active project implementation; comments state that fact.
- Existing eMMC export defect reproduced separately, without behavior edits in this slice: the C stream CRC update/finalization returns `0xCBF43926` for ASCII `123456789`, while the Python stream path using `binascii.crc32(payload, 0xFFFFFFFF) ^ 0xFFFFFFFF` returns `0x2DFD2D88`. Root notified for integration-scope disposition; this documentation-only slice does not claim successful eMMC serial export.

Documented file manifest:

- `.vscode/flash_board.sh`
- `CMakeLists.txt`
- `Inc/FreeRTOSConfig.h`
- `Inc/app_data_log.h`
- `Inc/app_emmc_log.h`
- `Inc/app_tasks.h`
- `Inc/ft5336_conf.h`
- `Inc/lvgl_port_lcd.h`
- `Inc/lvgl_port_touchpad.h`
- `Inc/main.h`
- `Inc/mt25tl01g_conf.h`
- `Inc/mt48lc4m32b2_conf.h`
- `Inc/stm32h745i_discovery_conf.h`
- `Inc/stm32h7xx_hal_conf.h`
- `Inc/stm32h7xx_it.h`
- `Src/Lvgl_Porting/lvgl_port_lcd.c`
- `Src/Lvgl_Porting/lvgl_port_touchpad.c`
- `Src/app_data_log.c`
- `Src/app_emmc_log.c`
- `Src/app_tasks.c`
- `Src/main.c`
- `Src/startup_stm32h750xx.S`
- `Src/stm32h7xx_hal_msp.c`
- `Src/stm32h7xx_it.c`
- `Src/syscall.c`
- `Src/sysmem.c`
- `Src/system_stm32h7xx.c`
- `cmake/gnu-tools-for-stm32.cmake`
- `cmake/vscode_generated.cmake`
- `dump_emmc_bin.py`
- `lv_conf.h`
- `stm32h750xb_flash.ld`
- `ui/CMakeLists.txt`
- `ui/components/ui_comp.c`
- `ui/components/ui_comp.h`
- `ui/components/ui_comp_hook.c`
- `ui/components/ui_comp_hook.h`
- `ui/components/ui_comp_label1.c`
- `ui/components/ui_comp_label1.h`
- `ui/fonts/ui_font_Mons110.c`
- `ui/fonts/ui_font_TomorrowBoldItalic12.c`
- `ui/fonts/ui_font_TomorrowBoldItalic14.c`
- `ui/fonts/ui_font_TomorrowBoldItalic18.c`
- `ui/fonts/ui_font_TomorrowBoldItalic26.c`
- `ui/fonts/ui_font_TomorrowBoldItalic64.c`
- `ui/images/ui_img_debugframe_png.c`
- `ui/images/ui_img_ellipse_6_png.c`
- `ui/images/ui_img_frame_1_png.c`
- `ui/images/ui_img_fs_dashbord_hard_png.c`
- `ui/images/ui_img_rectangle_24_png.c`
- `ui/images/ui_img_rectangle_27_png.c`
- `ui/images/ui_img_rectangle_6_png.c`
- `ui/images/ui_img_subtract_png.c`
- `ui/screens/ui_DebugScreen.c`
- `ui/screens/ui_DebugScreen.h`
- `ui/screens/ui_MainScreen.c`
- `ui/screens/ui_MainScreen.h`
- `ui/screens/ui_Screen1.c`
- `ui/screens/ui_Screen1.h`
- `ui/ui.c`
- `ui/ui.h`
- `ui/ui_events.h`
- `ui/ui_helpers.c`
- `ui/ui_helpers.h`

## CAN driver agent: production host tests

- Source inspection found that `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_fdcan.c` copies a DLC-derived payload before application-level validation. Its 16-entry DLC table reaches 64 bytes; the baseline application's receive array held eight bytes. Expanded the receive scratch array to 64 bytes so a subsequently rejected frame cannot overrun that application array.
- The baseline decoder accepted minimum rather than exact single-frame PCI lengths and did not reject extended identifiers, remote frames or FD format. The changed decoder requires the existing response ID, standard/classic/data metadata, exactly eight payload bytes, service 0x62, a known PID and its exact expected single-frame length from the existing request table. Valid repository-contract conversions and request scheduling remain unchanged.
- The host harness includes the actual `Src/app_can.c` and `Src/app_can_config.c`; fake HAL boundaries expose initialization failures/retry, FIFO errors/pressure, transmit backpressure and explicit millisecond time. The fake HAL copies the same DLC-derived byte count as the vendor HAL before decode. All 37 numeric FDCAN/RCC constants in the test header were independently compared with the vendor headers and corrected to match.
- Final sanitizer suite: **15 groups, 69,431 assertions, zero failures**, plus a separate maximum-payload bounds test. Coverage includes all ten PIDs, all 65,536 raw current values, all 256 SOC/average-temperature values, all PCI bytes for every PID, all DLC codes, metadata rejection, exact timeout boundaries, tick rollover, signed statistics/power and error recovery. This enumeration does not claim every possible system edge case.
- Independent review in [CAN_REVIEW.md](CAN_REVIEW.md) has no remaining required findings. A copied-source mutation restoring the eight-byte buffer failed the payload-bounds regression with **AddressSanitizer stack-buffer-overflow, WRITE of size 64**. The reviewer verified that the shared production/test files did not change during the mutation.
- Added a bounded stdin replay mode to the test executable for the root's virtual CAN pipeline tests. It preserves production statistics across records, validates line/hex/numeric/metadata bounds, and outputs JSON dashboard snapshots. Six independently checked malformed records exited with code 2 under sanitizers. The adapter does not open hardware.

## Simulator agent: virtual buses and repositories

- Inspected Src/app_can.c request table, transmit payload, receive decoder and numeric conversions. Implemented ten standard classic-CAN request PIDs on 0x7E3/0x7EB, services 0x22/0x62. This is the firmware implementation contract; no actual BMS specification is inferred. The user confirmed virtual-only inputs and deferred physical adapter/BMS/DBC information.
- Added tools/can_sim with strictly validated editable JSON tracks, step/linear interpolation, endpoint hold, timed pause/drop/delay, wrong ID/service/PCI, short payloads, unknown PID, duplicates/bursts and targeted raw boundaries. The scheduler takes explicit elapsed simulation time, bounds pending frames and rejects clock reversal. Every added Python function/method has a docstring and each module begins with a purpose statement.
- Primary source research: [python-can virtual](https://python-can.readthedocs.io/en/stable/interfaces/virtual.html) establishes same-process channel communication; therefore the runnable CLI demo creates both endpoints in one process. [Bus API](https://python-can.readthedocs.io/en/stable/bus.html#can.Bus) documents ignore_config=True, used to isolate external CAN configuration. Installed and pinned python-can 4.6.1; verified Python 3.9.6/macOS runtime.
- [Linux vcan](https://www.kernel.org/doc/html/latest/networking/can.html#the-virtual-can-driver-vcan) and [python-can SocketCAN](https://python-can.readthedocs.io/en/stable/interfaces/socketcan.html#the-virtual-can-driver-vcan) establish virtual traffic without hardware. The optional transport requires vcan-number naming, sysfs virtual path, CAN type 280, absence of a hardware device, and kernel link kind vcan. [Linux if_arp.h](https://github.com/torvalds/linux/blob/master/include/uapi/linux/if_arp.h) verifies type 280. Physical adapters and vxcan are rejected. Linux vcan runtime was not available on the macOS host; guard fixtures were tested.
- Repository research: [python-can](https://github.com/hardbyte/python-can) is the implemented dependency; [can-utils](https://github.com/linux-can/can-utils) is documented optional vcan observation/replay/generation tooling; [cantools](https://github.com/cantools/cantools) is a candidate after a DBC is supplied; [LVGL PC port v8.3](https://github.com/lvgl/lv_port_pc_eclipse/blob/release/v8.3/README.md) is an unimplemented UI candidate. Local lvgl/lvgl.h identifies LVGL 8.4.0. git ls-remote --heads verified release/v8.3 at f45decda90e8f89aa5d5b2d91a4ebc1f1d1a084a; compatibility with this UI remains unverified.
- [ST datasheet DS12556 Rev 8](https://www.st.com/resource/en/datasheet/stm32h750xb.pdf), table 8/page 88, supports PH13 FDCAN1_TX and PH14 FDCAN1_RX under AF9. [ST UM2488 Rev 10](https://www.st.com/resource/en/user_manual/um2488-discovery-kits-with-stm32h745xi-and-stm32h750xb-mcus-stmicroelectronics.pdf), page 26, maps PH13/D16 and PH14/B17 to FDCAN1 on the board. This supports the repository pin mapping, not actual future wiring or electrical safety.
- [QEMU STM32 docs](https://www.qemu.org/docs/master/system/arm/stm32.html) list F1/F2/F4/L4 machines and missing CAN; they do not establish exact STM32H750B-DK support. Reviewed [Renode repository](https://github.com/renode/renode) without sufficient evidence of full board/peripheral support. Neither emulator is selected or configured.
- Test-first evidence: initial .venv/bin/python -m unittest discover -s tests -p 'test_can_sim*.py' -v failed because tools.can_sim did not exist. After implementation, 29 tests passed. Added validation regression cases reproduced TypeError for list-valued fault type/target; fixed them with explicit string validation. Final focused suite: 31 tests passed in 0.187 seconds.
- Full editable edge-case demo: .venv/bin/python -m tools.can_sim --demo --scenario tools/can_sim/scenarios/edge_cases.json --rate 10 completed 20 simulation seconds. Recorded 276 accepted requests, 779 responses sent and received, and zero pending at stop; the trace included 13 wrong-ID and 14 short-DLC received frames. Counts depend on best-effort CLI timing and are not promised throughput.
- Added docs/CAN_SIMULATION.md with commands, units, fault semantics and function flow, plus docs/VIRTUAL_ENVIRONMENTS.md with hardware evidence, repository links, chosen dependencies and verification limits. Real adapter integration remains unimplemented.
