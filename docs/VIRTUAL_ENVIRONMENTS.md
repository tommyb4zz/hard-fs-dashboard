# Virtual environments and repository evidence

## Repository hardware target

README.md and .settings/ide.store.json identify **STM32H750B-DK**, **STM32H750XBH6** and **Arm Cortex-M7**. Src/app_can_config.c selects FDCAN1 with PH13 TX, PH14 RX and AF9; Src/app_can.c selects classic CAN. This identifies the repository target, rather than verifying connected hardware.

ST's [STM32H750 datasheet DS12556 Rev 8](https://www.st.com/resource/en/datasheet/stm32h750xb.pdf), table 8 on page 88, assigns PH13/PH14 to FDCAN1 TX/RX under AF9. The [kit manual UM2488 Rev 10](https://www.st.com/resource/en/user_manual/um2488-discovery-kits-with-stm32h745xi-and-stm32h750xb-mcus-stmicroelectronics.pdf), page 26, assigns PH13/D16 to FDCAN1_TX and PH14/B17 to FDCAN1_RX on the board. These sources support the configured mapping. Insufficient data to verify the eventual adapter, BMS, wiring, termination or protocol. Physical integration remains unimplemented for this virtual-only task.

## Repositories and verification

| Repository | Task use and selection reason | Hardware scope and verification |
|---|---|---|
| [hardbyte/python-can](https://github.com/hardbyte/python-can) | **Implemented and tested**, version 4.6.1. Supplies actual CAN message objects and a same-process virtual bus; optional SocketCAN supports Linux vcan. | Host traffic simulation, not Cortex-M7 instruction or FDCAN register emulation. [Official virtual documentation](https://python-can.readthedocs.io/en/stable/interfaces/virtual.html) states the same-process restriction. |
| [linux-can/can-utils](https://github.com/linux-can/can-utils) | **Documented optional Linux tooling.** candump observes/logs, cansend sends one frame, canplayer replays and cangen generates traffic. | Use local vcan0 for this task. Not installed or executed on this macOS host. |
| [cantools/cantools](https://github.com/cantools/cantools) | **Candidate for a supplied DBC.** Supports parsing, encoding/decoding and generated C pack/unpack functions. | No project DBC is supplied; not installed, and no actual BMS schema is inferred. |
| [LVGL](https://github.com/lvgl/lvgl), [PC port reference, release/v8.3](https://github.com/lvgl/lv_port_pc_eclipse/tree/release/v8.3) | **Local LVGL 8.4.0 implemented and tested** through the memory display in `tools/dashboard_host/render.c`. The official SDL port is a reference, not a dependency. | Actual project UI rendering/captures with fake peripherals; no STM32 instruction/peripheral emulation or physical LCD verification. |

Create an isolated Python environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/can_sim/requirements.txt -r tools/requirements-dev.txt
```

The current verified runtime is Python 3.13.7 and python-can 4.6.1 on macOS, with CMake 4.4.4 and host Clang. The [python-can version table](https://github.com/hardbyte/python-can) requires Python 3.9 or newer for version 4.6. See [SYNTHETIC_TESTING.md](SYNTHETIC_TESTING.md) for dashboard commands and verification boundaries.

## Linux virtual CAN environment

For separate-process tests, use a Linux host or VM exposing a local vcan interface. The [Linux kernel documentation](https://www.kernel.org/doc/html/latest/networking/can.html#the-virtual-can-driver-vcan) describes traffic without real controller hardware. Commands are in [CAN_SIMULATION.md](CAN_SIMULATION.md).

Linux vcan traffic has not been run on this macOS host. Its selection guard was tested with sysfs and kernel-link metadata fixtures. The same-process Python virtual bus was exercised directly.

## Exact board emulation

No complete STM32H750B-DK emulator is implemented or claimed. Host CAN tests execute production C logic behind a host HAL boundary; they do not emulate the MCU, LCD, external memory or full peripheral set.

The [official QEMU STM32 documentation](https://www.qemu.org/docs/master/system/arm/stm32.html) lists F1/F2/F4/L4 boards and identifies CAN as missing for those documented machines. It does not establish required STM32H750B-DK/Cortex-M7/FDCAN/display/external-memory coverage, so QEMU is not selected.

The [Renode repository](https://github.com/renode/renode) was inspected as a candidate. Insufficient data to verify complete STM32H750B-DK and required peripheral support from the reviewed sources. Renode is not selected, installed or configured.
