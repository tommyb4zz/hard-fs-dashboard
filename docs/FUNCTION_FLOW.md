# Function flow and source locations

The diagrams describe the inspected firmware source, not a measured device trace. Solid arrows show calls or documented processing order. Dotted arrows label callbacks, task scheduling or data handoff explicitly. Full definitions, line numbers and source comments are available in [FUNCTIONS.md](FUNCTIONS.md); [function-explorer.html](function-explorer.html) provides searchable incoming/outgoing relationships. [functions.mmd](functions.mmd) is the full editable Mermaid source graph and [functions.json](functions.json) is its machine-readable inventory.

## Firmware startup and task handoff

```mermaid
flowchart TD
  reset["Reset_Handler · Src/startup_stm32h750xx.S"] --> system["SystemInit · Src/system_stm32h7xx.c"]
  reset --> memory["Copy data / clear BSS / __libc_init_array"]
  memory --> main["main · Src/main.c"]
  main --> mpu["MPU_Config / caches / HAL_Init"]
  mpu --> clock["SystemClock_Config / DebugConsole_Init"]
  clock --> qspi["BSP_QSPI_Init / memory-mapped UI assets"]
  qspi --> start["app_tasks_start · Src/app_tasks.c"]
  start --> logs["app_data_log_init / app_emmc_log_init"]
  start --> queue["xQueueCreate · latest snapshot queue"]
  start --> tasks["xTaskCreate · GUI / CAN RX / console"]
  main --> scheduler["vTaskStartScheduler · FreeRTOS dependency"]
  tasks -. scheduled .-> gui["app_gui_task · Src/app_tasks.c"]
  tasks -. scheduled .-> can["app_can_rx_task · Src/app_tasks.c"]
  tasks -. scheduled .-> console["app_console_task · Src/app_tasks.c"]
  gui --> init["lv_init / LCD_init / touchpad_init / ui_init"]
  gui --> consume["xQueueReceive / app_ui_apply_dashboard_data"]
  gui --> auxiliary["app_ui_update_aux_placeholders / app_ui_apply_aux_state"]
  gui --> render["lv_tick_inc / lv_timer_handler / vTaskDelayUntil"]
  can --> caninit["app_can_init · Src/app_can.c · retry on failure"]
  can --> poll["app_can_poll_dashboard_data · Src/app_can.c"]
  poll -. valid snapshot .-> save["app_data_log_record / app_emmc_log_append"]
  save --> publish["xQueueOverwrite · latest snapshot"]
  publish -. queue handoff .-> consume
  console --> command["HAL_UART_Receive / app_console_handle_command"]
  command --> exports["RAM/eMMC inspect, dump, clear, flush; CAN debug toggle"]
```

The GUI task services LVGL every configured 5 ms; CAN polling delays 10 ms between iterations. The queue holds one latest snapshot. Screen bindings and the retained automatic speed/mode/status demo are in `Src/app_dashboard_ui.c`; the host supplies explicit auxiliary timelines through those bindings. Vehicle inputs remain unimplemented. `ui/screens/ui_Screen1.c` is retained legacy source and excluded from `ui/CMakeLists.txt`.

## CAN polling and decoding

```mermaid
flowchart TD
  poll["app_can_poll_dashboard_data · Src/app_can.c"] --> guard{"Initialized and non-null output?"}
  guard -->|No| stop["Return false; leave caller snapshot unchanged"]
  guard -->|Yes| timeout{"Pending request older than 100 ms?"}
  timeout -->|Yes| advance["Log timeout / clear pending / advance PID sequence"]
  timeout -->|No| fifo
  advance --> fifo{"RX FIFO has frames?"}
  fifo -->|Yes| read["HAL_FDCAN_GetRxMessage · 64-byte scratch buffer"]
  read --> handle["app_can_handle_rx_message"]
  handle --> decode["app_can_decode_orion_response"]
  decode --> shape{"0x7EB, standard classic data, DLC 8, service 0x62?"}
  shape -->|No| reject["Reject without publishing"]
  shape -->|Yes| length{"Known PID with exact PCI length?"}
  length -->|No| reject
  length -->|Yes| convert["Decode units; current updates current/power statistics"]
  convert --> retain["Commit latest dashboard data"]
  retain --> match{"Reply PID matches pending?"}
  match -->|Yes| clear["Clear pending / advance PID sequence"]
  match -->|No| fifo
  clear --> fifo
  reject --> fifo
  fifo -->|No / receive error| due{"Idle and at least 50 ms since request?"}
  due -->|Yes| transmit["app_can_send_pid_request · 0x7E3 / 0x22"]
  transmit --> track["On successful submission: set pending PID and timestamp"]
  due -->|No| result
  track --> result{"Any valid received update?"}
  result -->|Yes| out["Copy latest snapshot / return true"]
  result -->|No| stop
```

The request timeout is strictly greater than 100 ms in the code. Tick subtraction is unsigned to support wraparound. A valid unsolicited known PID updates telemetry; only a matching pending PID advances the request sequence. This is the repository's implemented protocol; actual BMS compatibility is unverified and outside the virtual-only scope.

## Display and input callbacks

```mermaid
flowchart LR
  lcd["LCD_init · Src/Lvgl_Porting/lvgl_port_lcd.c"] --> sdram["BSP_SDRAM_Init / LTDC and panel setup"]
  lcd -. registers flush_cb .-> flush["disp_flush · same file"]
  timer["lv_timer_handler · LVGL dependency"] -. invokes display callback .-> flush
  flush --> fb["Clean data cache / update LTDC framebuffer / flush ready"]
  touch["touchpad_init · Src/Lvgl_Porting/lvgl_port_touchpad.c"] -. registers read_cb .-> read["touchpad_read · same file"]
  timer -. invokes input callback .-> read
  read --> ts["BSP_TS_GetState / pressed or last released coordinates"]
  ui["ui_init · ui/ui.c"] --> screens["Main and Debug screen init · ui/screens/"]
```

## Virtual CAN test path

```mermaid
flowchart LR
  json["Editable JSON scenario · tools/can_sim/scenarios/"] --> scenario["Scenario.from_file / values_at · scenario.py"]
  client["make_request · protocol.py"] --> bus["python-can virtual bus · in-process endpoints"]
  bus --> runner["Runner.step · engine.py · explicit simulation clock"]
  scenario --> responder["Responder.plan · engine.py"]
  runner --> responder
  responder --> encode["encode_response · protocol.py · ten source-defined PIDs"]
  encode --> faults["Timed pause/drop/delay/malformed/duplicate/burst/raw faults"]
  faults --> busout["Virtual CAN response frames"]
  busout --> received["Client receives exact frame bytes and metadata"]
  received --> replay["Test runner --replay · tests/can_host/test_app_can.c"]
  replay --> fake["In-memory HAL FIFO and controlled millisecond clock"]
  fake --> production["Production app_can_poll_dashboard_data · Src/app_can.c"]
  production --> snapshots["JSON dashboard snapshots / assertions / ASan + UBSan"]
```

This path tests frame transport and the production C decoder. The host HAL does not emulate controller registers, arbitration, bus errors, electrical behavior, LCD/touch hardware, FreeRTOS scheduling or eMMC. Linux `vcan` is an optional virtual transport; physical adapters are rejected by the new simulator. New hardware integration remains unimplemented.

## Editable host dashboard

```mermaid
flowchart TD
  json["JSON · tools/can_sim/scenarios/"] --> scenario["Scenario.values_at / auxiliary_at · scenario.py"]
  run["run_scenario · tools/dashboard_sim/runner.py"] --> step["Host.step · explicit clock"]
  step --> main["main · tools/dashboard_host/main.c"]
  main --> poll["app_can_poll_dashboard_data · Src/app_can.c"]
  poll --> tx["Captured actual request · fake_hal.c"]
  tx --> virtual["python-can virtual endpoints"]
  virtual --> responder["Runner.step / Responder.plan · tools/can_sim/engine.py"]
  scenario --> responder
  responder --> virtual
  virtual --> step
  scenario --> aux["Typed synthetic speed / mode / seven statuses"]
  aux --> step
  main --> apply["app_ui_apply_dashboard_data / app_ui_apply_aux_state · Src/app_dashboard_ui.c"]
  apply --> lvgl["Actual generated UI and LVGL 8.4.0"]
  main --> logs["app_data_log_record / app_emmc_log_append · Src/"]
  logs --> storage["Memory MMC boundary · fake_hal.c"]
  lvgl --> capture["dashboard_render_capture · tools/dashboard_host/render.c"]
  capture --> viewer["PNG / viewer.html"]
  storage --> export["app_emmc_log_dump_binary / read_dump · dump_emmc_bin.py"]
```

The persistent C host is single-threaded. Screens and loggers are actual application code;
HAL/BSP and mutex boundaries are memory-only fakes. Python owns virtual transport and time.
This path does not execute FreeRTOS tasks or hardware startup.

## Recorded screen movement

The generated viewer inlines `tools/dashboard_sim/playback.js`. Its browser functions are
documented in source and included in the JavaScript/HTML inventory. Their inspected flow is:

```mermaid
flowchart TD
  mount["mountPlayback · playback.js:225"] --> clock["createPlayback · playback.js:24"]
  clock --> play["play / pause / seek / setSpeed / setLoop / replay"]
  play --> animate["animate · playback.js:119 · browser callback"]
  animate --> advance["advance · playback.js:98 · elapsed time × speed"]
  advance --> state["getState / frameIndex · choose recorded timestamp"]
  state --> update["updateControls · playback.js:325"]
  update --> request["requestCapture · playback.js:263"]
  request -. loaded .-> commit["commitCapture · playback.js:283"]
  commit --> pair["Replace loaded image / matching JSON state / capture timestamp"]
  request -. failed .-> reject["rejectCapture · playback.js:301"]
  reject --> stop["Seek failed timestamp / pause / clear image and state / show error"]
  controls["Viewer buttons / time slider / speed / loop"] --> play
```

The clock can advance between captures; the displayed state belongs to the loaded capture.
Asynchronous callbacks from older requests cannot overwrite a newer seek. Playback uses the
captured pixels and does not rerun the simulator. The default generator captures every 100 ms.
See [the playback source record](SYNTHETIC_PLAYBACK_UI_EVIDENCE.md) for API sources and regressions.

## Inventory interpretation

- The inventory covers project sources, headers, scripts and test tools. Generated font/image arrays receive file-purpose comments but contain no callable definitions.
- Dependency implementations in `Drivers/` and `lvgl/` retain their upstream documentation. Their call names remain visible as external call expressions in the explorer.
- Conditional C definitions are included without evaluating preprocessor configuration. Assembly entry labels are inventoried; the overview above shows their inspected cross-language startup calls.
- Calls resolve only when the symbol is unique or file-local. An identifier passed or assigned is a `reference`, which can indicate callback registration; execution is not inferred. Indirect or ambiguous calls remain unclassified.
- Production-source calls do not resolve to host-only HAL fakes. Host and test relationships are included separately in the same inventory; this prevents fake definitions from being presented as board implementations.
- Regenerate after source changes: `.venv/bin/python tools/generate_function_docs.py --check`.
