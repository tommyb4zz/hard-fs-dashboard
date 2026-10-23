# Complete code explanation work

## Source and scope

Read the existing function inventory, supplied MB1381 B04 CAD evidence, gap analysis and
synthetic integration/playback evidence first. The request is to expand comments and explain
the code. Preserve application behavior, existing user edits and vendor source licenses.

Explain every maintained function in `Src/`, `ui/`, tools and tests; cover headers, configuration,
startup, linker layout, assets and dependency boundaries in the code guide. Function comments
must state purpose, inputs/units, output, state changes and relevant failure/ordering behavior
where the implementation establishes them. Do not infer vehicle encodings or hardware success.
Upstream `Drivers/` and `lvgl/` keep their supplied implementation documentation; the guide
explains their role and the selected application entry points rather than claiming a new
line-by-line audit of every upstream dependency.

## Defined parts and shared evidence

1. Firmware agent: inspect and expand comments in `Src/app_*`, `Src/Lvgl_Porting/`, and their
   `Inc/` interfaces; write `docs/CODE_FIRMWARE_GUIDE.md` with source-grounded flow and BDP
   connections. End with a comment-only verification and evidence section in that shared file.
2. Synthetic agent: inspect and expand Python comments/docstrings in `tools/can_sim/`,
   `tools/dashboard_sim/`, `dump_emmc_bin.py`, and `tests/test_*.py` except function-doc tests;
   write `docs/CODE_SYNTHETIC_GUIDE.md`. End with AST behavior comparison and evidence there.
3. UI/host agent: inspect and expand `ui/`, `tools/dashboard_host/`, C test sources/headers,
   playback JavaScript and its JS tests; write `docs/CODE_UI_HOST_GUIDE.md`. End with source
   comparison and evidence there. Do not edit renderer HTML or regenerate inventory.
4. Root: explain remaining startup, syscall, interrupt, build/linker/configuration and function
   explorer/generator sources; assemble `docs/CODE_GUIDE.md`, refresh inventory/graphs, check
   all maintained function explanations and shared reports, and run appropriate verification.

Each agent reads its owned implementations before editing. Shared reports are in `docs/`;
the root checks them against source and BDP evidence. No overlapping implementation ownership,
physical interfaces, flashing, commits or remote writes. Baseline sources for comparison are
preserved in ignored `build/documentation-review/baseline.json` before edits.

## Verification

Compare C token sequences and Python ASTs excluding docstrings against the baseline. Changes
to documentation tooling are tested separately. Regenerate links after comment insertion;
check purpose headers and function coverage; run host/synthetic tests and formatting checks.
Working synthetic execution and BDP design agreement do not certify physical hardware behavior.
