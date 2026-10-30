# Startup, platform and build code explained

## Reset to application

[startup_stm32h750xx.S](../Src/startup_stm32h750xx.S) defines the vector table and two entry
routines. The first vector supplies `_estack`; the second points to `Reset_Handler`. The linker
retains `.isr_vector` in internal flash. Weak peripheral aliases use `Default_Handler` unless a
linked implementation replaces them. The table lists possible interrupts; it does not enable
them or establish every peripheral is used.

`Reset_Handler` sets SP, calls `SystemInit`, copies initialized words from `_sidata` into
`[_sdata, _edata)`, clears words in `[_sbss, _ebss)`, invokes `__libc_init_array` and calls `main`.
The copy/clear loop labels are local control-flow locations, not separate callable functions.
If main returns it loops forever. `Default_Handler` also loops forever without peripheral
acknowledgment or a recovery policy. This assembly depends on linker-aligned boundaries and
does not probe the validity of mapped memory.

[system_stm32h7xx.c](../Src/system_stm32h7xx.c) supplies CMSIS platform functions:

| Function | Current behavior |
|---|---|
| `SystemInit` | Executes before initialized globals are ready. Enables FPU access when selected, restores RCC defaults, applies the selected silicon-revision AXI workaround, disables reset FMC bank access and chooses VTOR. Optional defines can enable D2 SRAM or the early external-memory helper. |
| `SystemCoreClockUpdate` | Reads the selected clock source, PLL/fraction/dividers and prescaler table; updates `SystemCoreClock` and `SystemD2Clock` in Hz. A zero PLL M divisor yields zero. Constants must match real oscillator sources; no frequency measurement occurs. |
| `SystemInit_ExtMemCtl` | Conditional early SDRAM register/GPIO setup when `DATA_IN_ExtSDRAM` is defined. Uses fixed command/timing values and countdown waits without a returned error. The normal display initialization uses the BSP path instead. |
| `ExitRun0Mode` | Conditional vendor power-mode selector. Compiled supply branches update PWR and wait without a timeout; no matching branch performs no action. Its definition alone does not establish invocation by this build. |

The optional early external-memory defines are commented out in this file. Do not confuse this
vendor reset preparation with the checked, later clock configuration in main.

## Main and memory policy

[main.c](../Src/main.c) has six functions, each expanded inline:

| Function | Inputs, order and outcome |
|---|---|
| `main` | No parameters. MPU → caches → HAL → clocks → debug console → QSPI init/mapping → barriers/cache invalidation → tasks → scheduler. QSPI/task errors or scheduler return halt. UI code/assets linked to QSPI require that image to be programmed before use. |
| `SystemClock_Config` | Select LDO/scale 1, check readiness with unsigned HAL milliseconds and a strict `>100` timeout, configure HSE/PLL1/bus dividers and flash latency. With configured 25 MHz HSE, requested CPU/HCLK/APB rates calculate to 400/200/100 MHz. Configuration is not measured operation. |
| `DebugConsole_Init` | Zero-init configuration, initialize COM1 at 115200 8N1 without flow control and select BSP log port. Check both BSP results; errors halt. This is distinct from the export stream format. |
| `MPU_Config` | Disable MPU, configure four numbered cacheable/full-access/executable regions, enable privileged default map. Reusing one descriptor preserves common attributes. Region size sets CPU policy, not fitted memory capacity. |
| `Error_Handler` | Disable maskable interrupts and execute NOP forever; no reset, fault capture or physical-output policy. |
| `vApplicationStackOverflowHook` | Ignore task handle/name and enter the fatal handler when kernel stack checking invokes the callback. |

The MPU maps AXI SRAM at `0x24000000`/512 KiB, D2 SRAM at `0x30000000`/256 KiB, SDRAM at
`0xD0000000`/32 MiB and QSPI at `0x90000000`/128 MiB. BDP evidence gives 8 MiB accessible SDRAM;
the discrepancy is a bounds/profile follow-up, not demonstrated memory access failure. The
linker declares 288 KiB D2 SRAM while this MPU region covers 256 KiB; a declaration alone does
not mean the application allocates or accesses the remaining range.

## Exceptions and peripheral interrupts

[stm32h7xx_it.c](../Src/stm32h7xx_it.c) provides twelve active definitions. `NMI_Handler`,
`HardFault_Handler`, `MemManage_Handler`, `BusFault_Handler` and `UsageFault_Handler` each stop
in an infinite loop without recording hardware fault status. `DebugMon_Handler` returns without
action. These bodies do not diagnose a root cause or implement recovery.

`SVC_Handler` delegates to `vPortSVCHandler`; `PendSV_Handler` delegates to `xPortPendSVHandler`.
The selected FreeRTOS port owns exception-frame/context semantics. `SysTick_Handler` always
increments HAL time and calls `xPortSysTickHandler` when the scheduler state differs from
`taskSCHEDULER_NOT_STARTED`. That guard supports startup HAL delays before kernel scheduling;
it is not a claim that suspended scheduling prevents all tick bookkeeping.

`SDMMC1_IRQHandler` calls `BSP_MMC_IRQHandler(0)`. `EXTI15_10_IRQHandler` calls the BSP user-button
handler. `MDMA_IRQHandler` delegates to the SDRAM BSP instance zero. These wrappers do not
initialize transfers or directly export logs. Remaining commented-out examples do not execute.
Declarations in [stm32h7xx_it.h](../Inc/stm32h7xx_it.h) do not supply missing implementations.

[stm32h7xx_hal_msp.c](../Src/stm32h7xx_hal_msp.c) has one active `HAL_MspInit`: enable the SYSCFG
clock and return. Its retained line-commented peripheral examples are inactive. The active CAN,
display and BSP resource setup lives in their own modules, not those examples.

## Libc compatibility and allocation

[syscall.c](../Src/syscall.c) supplies minimal embedded compatibility symbols. It implements
neither a process manager nor a filesystem:

| Functions | Behavior |
|---|---|
| `initialise_monitor_handles`, `_getpid` | Empty monitor setup; fixed process ID 1. |
| `_kill`, `_exit` | `_kill` ignores inputs and returns -1/EINVAL; `_exit` calls it then loops forever. No MCU reset or log flush. |
| `_read`, `_write` | Weak hooks loop over `len` bytes through linked `__io_getchar`/`__io_putchar`, ignore descriptor and low-level errors, return requested length. Buffer/length validation and serialization are absent here. |
| `_fstat`, `_stat`, `_isatty` | Character-device mode/terminal-like stubs. Stat updates only `st_mode`; caller pointers must be valid. No actual descriptor/path lookup. |
| `_close`, `_open`, `_lseek`, `_times` | Close/open return -1, seek returns zero and times returns `(clock_t)-1`; these bodies do not set errno. No underlying operation is performed. |
| `_wait`, `_unlink`, `_link`, `_fork`, `_execve` | Return -1 with ECHILD, ENOENT, EMLINK, EAGAIN, ENOMEM respectively; ignored inputs are not dereferenced. |
| `starm_putc`, `starm_getc` | Picolibc-only FILE callbacks to the character hooks. Output discards hook status; input converts through unsigned char so a negative EOF/error is not preserved. Standard streams share the configured static FILE and aliases. |

[sysmem.c](../Src/sysmem.c) supplies `_sbrk(incr)`. It lazily starts at `_end`, returns the old
watermark and moves by signed bytes. Growth above `_estack - _Min_Stack_Size` fails with ENOMEM
and `(void *)-1`. It has no lower-bound shrink check or task synchronization. This libc heap is
separate from FreeRTOS heap_4 and LVGL's configured allocator. A reserved main-stack size is a
linker policy, not a measured worst-case stack depth. Picolibc uses an alias of the same hook.

## Configuration headers

| File | Role |
|---|---|
| [main.h](../Inc/main.h) | HAL/BSP includes, board GPIO aliases and fatal-hook declarations. Including it does not configure hardware. |
| [FreeRTOSConfig.h](../Inc/FreeRTOSConfig.h) | Preemption/time slicing, 1 kHz ticks, seven priorities, 64 KiB dynamic heap, mutexes/notifications and stack check mode 2. Kernel assertions halt through Error_Handler. Timers/idle/tick hooks are disabled. Minimal task stack counts are kernel stack elements, not bytes. |
| [stm32h7xx_hal_conf.h](../Inc/stm32h7xx_hal_conf.h) | Enabled HAL module headers, callbacks, oscillator constants and optional assertions. HSE is configured 25 MHz; HAL `USE_RTOS=0` does not disable the separately linked kernel. |
| [stm32h745i_discovery_conf.h](../Inc/stm32h745i_discovery_conf.h) | BSP COM logging, LCD layer addresses at `0xD0000000` and `0xD0200000`, touch features and IRQ priorities; retained audio options do not establish dashboard audio execution. |
| [ft5336_conf.h](../Inc/ft5336_conf.h) | Disabled auto-calibration and 480×272 coordinate limits; orientation/read callbacks are in the touch port. |
| [mt48lc4m32b2_conf.h](../Inc/mt48lc4m32b2_conf.h) | SDRAM refresh count `0x0603` and timeout `0xFFFF`; retained refresh comment refers to 100 MHz SDRAM clock. Part/capacity verification is separate. |
| [mt25tl01g_conf.h](../Inc/mt25tl01g_conf.h) | Read dummy-cycle counts for STR/DTR flash commands. Legacy driver naming is not a fitted-device ID. |
| [lv_conf.h](../lv_conf.h) | LVGL color, memory, tick, fonts/widgets and optional features shared by host/firmware; detailed existing option comments remain. RGB565 changes color precision; display buffers belong to each port. |

## Build and image layout

[CMakePresets.json](../CMakePresets.json) defines Debug/Release with Ninja, separate build folders,
Cortex-M7, double-precision FPU and little-endian target fields. JSON has no comment syntax; its
fields are explained here instead of adding invalid inline comments.

[gnu-tools-for-stm32.cmake](../cmake/gnu-tools-for-stm32.cmake) derives CPU/FPU/endian flags from
those fields, selects `arm-none-eabi-*` tools from PATH, marks the target Generic ARM and uses
static-library try-compiles. Other CPU branches are reusable toolchain choices; they do not
change this project's selected MCU.

[CMakeLists.txt](../CMakeLists.txt) is the active firmware build: C11/C++20, application sources,
selected HAL/BSP/component sources, generated UI, LVGL and a configured FreeRTOS source tree.
`configure_embedded_library(target_name)` applies shared CPU/section flags and assembly
preprocessing to an existing target; it ignores absent targets and returns no value. Debug
and Release use `-Os`; Debug additionally has symbols. Section garbage collection removes
unreferenced linker sections. The build checks for kernel `tasks.c` rather than downloading it.

The kernel source list includes event groups, lists, queues, tasks, ARM_CM4F port and heap_4.
This describes the selected configuration, not evidence of physical scheduling/stack behavior.
[vscode_generated.cmake](../cmake/vscode_generated.cmake) retains converter lists; the active root
build does not include that file.

[stm32h750xb_flash.ld](../stm32h750xb_flash.ld) places startup/application in 128 KiB internal
flash, LVGL/UI text/constants in `.extflash` at mapped QSPI, and retained `.ui_assets` there too.
Initialized `.data`/`.tdata` have flash load addresses and RAM_D1 run addresses. NOLOAD `.bss`/
`.tbss` are cleared by startup; heap/stack symbols reserve room in RAM_D1. Other MEMORY entries
are declarations and do not automatically allocate data. The linker keeps constructor arrays
and exports the bounds used by reset and libc.

Post-build objcopy produces addressed HEX, internal binary excluding QSPI sections and a QSPI
binary with `0x90000000` subtracted from its addresses. Compilation produces files; it does not
program physical flash.

[flash_board.sh](../.vscode/flash_board.sh) inspects overrides/PATH/bundle locations through
`find_programmer()` and `find_loader(programmer_path)`. The former returns an executable path;
the latter checks loader file presence. Both return failure when discovery fails. Neither
establishes compatibility with a fitted QSPI device. After checking both binary files, the
script's final commands program/verify QSPI first, then internal flash and reset. That physical
action is outside synthetic/documentation tests; the script was not executed here.

Editor settings/launch/tasks files select tool paths, build/program/debug commands and workspace
configuration. They do not become firmware logic. Existing user edits to editor configuration
remain outside this documentation pass.

## Documentation tooling

[generate_function_docs.py](../tools/generate_function_docs.py) runs only on the desktop. Its
input is maintained source; output is inert JSON, a Markdown inventory, a full Mermaid graph
and an offline explorer. It does not execute the application to discover functions.

- `walk` traverses named syntax descendants; `clean_comment` removes comment markup and joins
  existing explanation text for display.
- `collect_c_functions` uses actual C syntax, adjacent comments, declarator names and call/
  reference expressions. It retains conditional definitions without running a preprocessor.
- `collect_python_functions` uses the standard AST, scope-qualified names and own docstrings.
  Its `body_calls` excludes nested definitions; `visit` inventories their bodies separately.
- `collect_javascript_functions` uses Tree-sitter declarations/methods/arrows/callbacks. Its
  `adjacent_description` looks at the function or enclosing statement comments without taking
  an outer function's contract; `body_expressions` keeps nested calls in their own scope.
- `collect_html_functions` extracts this repository's executable inline scripts, skips inert
  data/external scripts and adds the original HTML line offset. It fetches no remote script.
- `collect_other_functions` recognizes bounded shell/CMake/assembly definition patterns; internal
  assembly loop labels and object symbols are excluded. It does not infer runtime calls there.
- `resolve_edges` resolves only eligible unique names, preferring local/visible scope and
  excluding test/host substitutions from production links. Dynamic members stay unclassified.
- `project_sources` selects maintained source/templates and excludes upstream/local builds;
  `collect_inventory` reads them, checks purpose headers and assembles definitions/edges/gaps.
- `write_outputs` writes the generated artifacts and escapes `<` in embedded JSON; `main`
  handles paths/--check, prints counts/gaps and returns failure for gaps only under --check.

The JavaScript parser is pinned to
[tree-sitter-javascript 0.23.1](https://pypi.org/project/tree-sitter-javascript/0.23.1/) beside the
existing parser versions. Syntax-node traversal follows the
[official Tree-sitter Python interface](https://tree-sitter.github.io/py-tree-sitter/classes/tree_sitter.Node.html).
The installed combination was exercised by the parser regressions; no newer-parser compatibility
is assumed. See [test_function_docs.py](../tests/test_function_docs.py) for active-vs-commented
syntax, pointer returns, scopes/callbacks, missing explanations, original HTML lines and fake
HAL exclusion checks.

[function_explorer.html](../tools/function_explorer.html) is the generator template. Its inert
JSON script contains inventory data; its executable script provides:

| Function | DOM behavior |
|---|---|
| `setText` | Assign literal text to a known existing ID; does not interpret comment strings as HTML. |
| `functionButton` | Create a button with source location/edge kind; clicking selects a source ID rather than executing that function. |
| `renderList` | Apply case-insensitive text/exact file filters, rebuild buttons and update matched count. |
| `addSvg` | Create an SVG element with literal attributes/text and append it to its parent. |
| `drawGraph` and nested `node` | Draw up to twelve incoming/outgoing nodes per side with solid/dotted arrows and keyboard navigation; complete lists retain every edge. |
| `selectFunction` | Show comment/location, complete incoming/outgoing relationships and unresolved calls; redraw selection without clearing filters. |

Anonymous callbacks index items, filter/project edges, construct nodes/options and handle click/
keyboard selection. Their expanded comments and source-location labels appear in the inventory.
CSS provides responsive layout, focus styling and scrollable large graphs. The generated explorer
in `docs/` works offline; templates are not themselves completed inventories.

## Sources and verification record

The reset/main/system/interrupt/libc/config/build/linker bodies listed above were read directly.
BDP comparisons use the supplied CAD evidence rather than guessed fitted hardware. Platform
function comments were expanded without changing executable tokens. Documentation tooling adds
JavaScript/HTML inventory coverage; this intentional tooling behavior change has parser regressions.
Final combined evidence is in [CODE_EXPLANATION_EVIDENCE.md](CODE_EXPLANATION_EVIDENCE.md).
