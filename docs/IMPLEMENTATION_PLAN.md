# Documentation and CAN simulation implementation plan

## Source baseline

Work starts from `origin/develop` at `6b09c08` in branch `codex/can-documentation-tests`. The original checkout has unrelated Git history and two existing local edits; it is preserved.

Current checkout after the user's location correction: `/Users/youseffarahat/Documents/hard-fs-dashboard` is on `develop` at the fetched `6b09c08`, with all completed implementation changes applied locally. The two existing edits are restored; the former local development history is preserved in `codex/develop-before-sync-20261010`. Current verification results are recorded in `RESEARCH.md`.

## Evidence and scope

- `README.md`, `.settings/ide.store.json`, and `CMakePresets.json` identify STM32H750B-DK / STM32H750XBH6 / Cortex-M7.
- `Src/app_can_config.c` selects FDCAN1, PH14 RX, PH13 TX, AF9, classic CAN.
- `Src/app_can.c` implements requests on 0x7E3, responses on 0x7EB, service 0x22/0x62, ten PIDs, 50 ms request spacing and a timeout after 100 ms.
- The source is evidence of the implemented protocol, not proof of the connected BMS protocol or safe electrical wiring. Insufficient data to verify the connected transceiver, vehicle DBC, termination, physical pin routing, or actual BMS model.
- User clarified that the physical adapter/BMS/DBC will be supplied later. Implement virtual input testing only; leave new physical input integration unimplemented.
- Document project-owned C/C headers, assembly, Python, shell and CMake sources. Preserve upstream dependency source and its existing comments in `Drivers/` and `lvgl/`; explicitly inventory that boundary.
- Exercise the production CAN C source through a host HAL boundary, and use python-can virtual/SocketCAN vcan for configurable time-varying bus traffic. Do not silently select physical CAN interfaces.

## Ordered work and ownership

1. **Function documentation**: inspect every project function; add factual purpose/behavior comments and file headers without behavior changes. Owner: documentation agent. Excludes `Src/app_can.c`, `Inc/app_can.h`, `Src/app_can_config.c`, `Inc/app_can_config.h`, and new simulation/test tooling.
2. **Production CAN tests**: compile `Src/app_can.c` with a host HAL test boundary, write failing edge-case tests, fix reproduced validation/buffer issues, and document each changed function. Owner: CAN driver agent. Owns CAN files and `tests/can_host/`.
3. **CAN simulator**: write tests first, implement editable JSON timelines with interpolation and fault injection, provide virtual and explicitly restricted vcan transport, and verify primary-source repositories. Owner: simulator agent. Owns `tools/can_sim/`, simulator tests and CAN setup guide.
4. **Function graph and integration**: derive inventory/call graph from inspected source with callback relationships distinguished, run host tests and firmware builds, review all changes and consolidate evidence. Owner: root agent.

Each agent updates its assigned section of `docs/RESEARCH.md` with exact source locations, decisions, commands and outcomes. Dependencies are integrated after their owner finishes.

## Acceptance criteria

- Each project function has an explanatory comment or Python docstring and each project script/source file has a purpose header.
- A regenerable function inventory identifies file locations; a flowchart explains startup, tasks, receive/decode/UI/logging and simulation flow.
- Host tests exercise production receive/decode/request behavior, including malformed frames, current/SOC/temperature boundaries, timeout and tick rollover, FIFO pressure and recovery.
- Simulator inputs are editable over time and support reproducible fault scenarios; tests prove virtual CAN communication.
- Physical CAN is never used implicitly. Physical hardware safety is reported as unverified until electrical and protocol evidence exists.
- Record exact commands and actual results, including any build/tooling limitations. No claim of exhaustive edge-case coverage or verified physical hardware without evidence.
