# hard-fs-dashboard

## Live screens and malfunction history

The dashboard uses received CAN measurements with per-signal validity/freshness; missing or
stale values show `--`. The main screen and the red cell table are the SquareLine layouts as
exported (the table is the original DebugFrame image with its own row captions). A pack screen
and an append-only malfunction history are added at runtime from `Src/app_diagnostics_ui.c` in
the same style, so re-exporting from SquareLine does not remove them. Malfunctions are recorded
as episodes, one entry when a problem starts and one CLEAR when it ends, in a checksummed eMMC
journal of about a million entries. See [runtime behavior, navigation, persistence and
verification](docs/DASHBOARD_RUNTIME.md).

Try the real firmware code interactively in a browser (taps and long presses go to its touch
handling):

```sh
.venv/bin/python -m tools.dashboard_sim --live --scenario tools/can_sim/scenarios/dashboard.json --open
```

Or record a run and replay it with a screen selector and a `fault-history.csv` download:

```sh
.venv/bin/python -m tools.dashboard_sim --scenario tools/can_sim/scenarios/dashboard.json --output build/dashboard-complete --capture-every-ms 500 --open
```

On the board, console command `j` streams the whole malfunction history as CSV over the
debug UART. Firmware auxiliary inputs (speed, drive mode, IMD/BSPD/... status) have no
provider yet: they show unknown and are not written to the history until one is wired.

## Function documentation and virtual CAN testing

- [Complete code guide](docs/CODE_GUIDE.md): expanded function contracts, module explanations, startup/build, synthetic tests, UI and BDP connections.
- [Function flowcharts](docs/FUNCTION_FLOW.md), [function inventory](docs/FUNCTIONS.md), and [interactive function explorer](docs/function-explorer.html).
- [Virtual CAN scenarios and test commands](docs/CAN_SIMULATION.md).
- [Editable synthetic dashboard: inputs, rendered timeline and log export](docs/SYNTHETIC_TESTING.md).
- [Verified virtual environment repositories and hardware scope](docs/VIRTUAL_ENVIRONMENTS.md).
- [MB1381 BDP review: missing code, required information and implementation locations](docs/BDP_GAP_ANALYSIS.md).
- [Implementation and test evidence](docs/RESEARCH.md).

Project functions include explanatory comments and script/source files include purpose headers. Upstream `Drivers/` and `lvgl/` implementations retain their dependency documentation. New input testing uses virtual CAN; physical adapter, wiring and vehicle protocol integration remain unimplemented.

From the repository root:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/can_sim/requirements.txt -r tools/requirements-dev.txt
.venv/bin/python -m tools.can_sim --demo --scenario tools/can_sim/scenarios/normal.json
.venv/bin/python -m tools.dashboard_sim --scenario tools/can_sim/scenarios/user-inputs.json --open
.venv/bin/python -m unittest discover -s tests -p 'test_*.py' -v
.venv/bin/python tools/generate_function_docs.py --check
```

The dashboard command builds a host executable and writes actual LVGL captures, an animated viewer,
decoded snapshots and validated log exports to `build/dashboard-sim/`. Edit the JSON and rerun to
change inputs over time. `--open` opens the viewer in your default browser. The synthetic auxiliary helper is retained for explicit tests; it is not called by production firmware.

The Python integration tests require CMake and a host Clang/GCC compiler with sanitizer support.
See [production CAN host tests](tests/can_host/README.md) for the independent C suite.

`hard-fs-dashboard` is an STM32H750B-DK firmware project built with STM32Cube for VS Code, CMake, LVGL, and a SquareLine-generated UI.

The project runs on the Cortex-M7 core and produces:
- an internal flash image for `0x08000000`
- a QSPI image for `0x90000000`

Main folders:
- `Src/`: application code and LVGL porting
- `ui/`: SquareLine-generated UI layer
- `lvgl/`: LVGL library
- `Drivers/`: STM32 HAL, BSP, and vendor code

## Team Rules

Development flow:
- do not work directly on `main`
- do not push feature work directly to `develop`
- always create a branch from `develop`
- always merge into `develop` through a pull request

Branch flow:

```bash
git switch develop
git pull origin develop
git switch -c feature/<short-description>
```

Before opening a pull request:

```bash
pre-commit run --all-files
git add .
git commit -m "Short clear message"
git push -u origin feature/<short-description>
```

Pull request target:
- source branch: `feature/<short-description>`
- target branch: `develop`

Branch intent:
- `main`: stable branch
- `develop`: integration branch

## Formatting

This repository uses `clang-format` together with `pre-commit`.

Configuration files:
- `.clang-format`
- `.clang-format-ignore`
- `.pre-commit-config.yaml`
- `.github/workflows/clang-format.yml`

### Install

With Homebrew:

```bash
brew install clang-format pre-commit
```

Or:

```bash
pip install pre-commit
```

### Setup

Run once after cloning:

```bash
pre-commit install
pre-commit run --all-files
```

If `pre-commit` reports `files were modified by this hook`, that is expected. Review the diff, stage the changes, and run it again until it passes.

### Formatting Scope

Formatting applies only to project-owned code:
- `Src/`
- `ui/`

Formatting is intentionally excluded for:
- `Drivers/`
- `Inc/`
- `lvgl/`
- `ui/images/`
- `ui/fonts/`

This keeps generated code, vendor code, and generated UI assets unchanged.

### Useful Commands

Run formatting on all tracked files in scope:

```bash
pre-commit run --all-files
```

Show exactly which files are being checked:

```bash
pre-commit run clang-format --all-files -v
```

## Build

The project uses CMake presets and STM32Cube for VS Code.

Prerequisites: an `arm-none-eabi` GCC toolchain and a FreeRTOS kernel. The STM32 VS Code
extension provides both (toolchain on `PATH`, FreeRTOS pack under
`~/Library/Application Support/stm32cube/packs`). Without it, run
`tools/fetch_firmware_deps.sh`: it fetches FreeRTOS-Kernel V10.6.2 into `build/_deps` and checks
for a toolchain, which CMake also looks for in `build/_deps` and `/Applications/ArmGNUToolchain`.
`-DFREERTOS_PACK_DIR=<dir>` overrides the kernel location.

Typical local flow:
- configure with the `Debug` preset
- build from VS Code or with CMake
- use the VS Code tasks to clean, build, and flash

Available VS Code task flows include:
- configure
- clean
- build
- flash
- build and flash
- clean build and flash

## Flashing

The project is programmed in two parts:
- internal flash image at `0x08000000`
- QSPI image at `0x90000000`

The recommended flow is to use the included VS Code tasks:
- `Flash Board`
- `Build and Flash Board`
- `Clean Build and Flash Board`

These tasks:
- program the QSPI image first
- then program the internal flash image
- reset the board at the end

Generated files:
- `build/Debug/hard-fs-dashboard.bin`
- `build/Debug/hard-fs-dashboard_qspi.bin`

The flash task uses `STM32_Programmer_CLI` and the `MT25TL01G_STM32H750B-DISCO.stldr` external loader. If the tools are not found automatically, set one of these environment variables:
- `STM32_PROGRAMMER_CLI`
- `STM32_EXTERNAL_LOADER`
- `CUBE_BUNDLE_PATH`
