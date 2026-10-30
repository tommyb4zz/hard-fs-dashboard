# Editable synthetic dashboard test mode

## Confirmed request

Keep synthetic inputs for testing and allow the user to supply and vary them over time. Retain the existing automatic firmware demo. Add an explicit host test path using editable JSON, the production CAN request/response logic, actual LVGL screens and fake storage. Physical input binding remains deferred. This host path does not emulate electrical CAN, FreeRTOS scheduling or MCU registers.

## Contract

- Existing scenario `signals` and `faults` retain their meaning.
- New optional `auxiliary` contains `speed_kph`, `drive_mode`, and `statuses`. Speed is null (unknown) or finite 0..65535, constant or step/linear points. Drive mode is null or N/D/R, constant or step points. Each status is unknown/ok/fault, constant or step points. Status keys: imd, fault, rtd, tsal, bspd, sdc, ready_to_drive. Omitted auxiliary inputs are unknown. These are synthetic test definitions, not inferred vehicle encodings.
- A portable UI binding exposes `app_aux_ui_state_t`, existing mode/status IDs, `app_ui_apply_dashboard_data`, `app_ui_apply_aux_state`, and `app_ui_update_aux_placeholders` in `Inc/app_dashboard_ui.h` / `Src/app_dashboard_ui.c`. Status state adds unknown alongside the existing fault boolean through a per-status validity boolean; existing demo sets validity true. The firmware GUI keeps calling the existing demo provider. Host input supplies its own explicit state. Unknown speed/mode render as dashes; unknown statuses remain visible with a distinct neutral color.
- A persistent host executable exchanges JSON-compatible command results via stdin/stdout. Python advances deterministic time, sends actual C-generated requests over python-can virtual, delivers scenario responses to C, applies auxiliary state, records samples, and optionally renders framebuffer captures and exports fake eMMC records. No physical adapter/backend is offered.
- Host command protocol is specified by root before integration. Image captures use the checked-in LVGL renderer with a memory display; no SDL dependency is required for reproducible captures.

## Bounded ownership

1. **Scenario agent:** extend only scenario parsing and auxiliary tests/examples/documentation. Test validation, constants, step/linear time sampling, unknown states, and backwards compatibility. Finish when the contract is implemented with focused tests passing.
2. **UI agent:** factor existing UI helpers/bindings/demo from `Src/app_tasks.c` into the portable module without changing CAN/console/task behavior. Add unknown rendering and fix reproduced usage-percent overflow. Add a small host renderer and host CMake target scaffolding that root can connect to CAN/storage. Do not change the power indicator's meaning without a requirement. Finish with portable rendering evidence and firmware build compatibility.
3. **Storage agent:** fix reproduced partial-flush export indexing and Python stream CRC; add permanent production-C-to-Python regressions. Preserve storage format where possible, document handling of existing sessions. Own eMMC/export files and dedicated storage tests only. Finish with positive/negative export tests and sanitizers passing.
4. **Root:** implement and test the closed-loop host bridge/runner using the above contracts; inspect real captures, integrate firmware builds, refresh function graph and BDP status, review final changes and record exact evidence.

Each agent records source evidence, commands, decisions and results in its own `docs/SYNTHETIC_*_EVIDENCE.md`. These form the shared research record. No commits, pushes, flashing or hardware I/O are part of this task.

## Acceptance and unresolved requirements

User JSON changes must reach production decoded battery state and actual screen labels/status colors. Synthetic parts remain available. Deterministic pause/malformed/duplicate traffic still reaches the decoder. Logging/export must round-trip correctly across repeated partial flushes. All new project functions/scripts require explanatory comments/purpose headers. Existing tests and Debug/Release firmware must pass.

Final physical schemas, digital polarity, stale deadlines, alert thresholds, power-bar units, runtime bus-off recovery and full peripheral verification remain explicit gaps. Do not call a host run proof that the complete hardware works.
