# Physical board: live inputs and demo startup

Live mode uses accepted CAN replies from the slave/BMS. Demo mode uses generated
engineering values, visibly labelled DEMO on every screen. A working LCD and touch panel
do not establish that a slave uses the same CAN protocol.

## Starting either mode

Connect a USB data cable from the Mac to the STM32H750B-DK CN14 ST-LINK connector.
From the repository root, run `./tools/start_board.sh live` or
`./tools/start_board.sh demo`. CMake and the ARM compiler are required as for the normal
firmware build. The script builds Debug or Demo, writes QSPI first and internal flash
second, verifies both, and resets to run. It uses STM32CubeProgrammer when installed,
otherwise OpenOCD's board driver. Failed QSPI identification stops programming.

For a demo without reflashing a live build: hold blue USER, press and release black RESET,
and keep USER held until startup. Normal reset without USER returns to live CAN.
The dedicated Demo build always selects demo; flash `live` to restore normal startup.
The selection is latched for the boot, so demo and live measurements cannot mix.

The demo cycles temperature, power, SOC, pack voltage/current, cell values and speed over
40 seconds, and briefly shows a synthetic IMD indicator. These values are examples,
not pack limits. Demo does not start the CAN task, initialize eMMC, attach the fault journal,
or record synthetic episodes. The FAULTS page says history is disabled. Console `s`
reports `DEMO synthetic; CAN=off storage=off` or `LIVE CAN; synthetic=off`.

## Implemented slave contract

The following comes from `Src/app_can.c`, `Src/app_can_config.c` and the checked-in decoder
tests. It is the dashboard's contract; the actual slave must implement the same contract.

- MCU controller: FDCAN1, PH13 TX / PH14 RX, AF9.
- Configured classic CAN, standard 11-bit identifiers, 500 kbit/s calculated from 25 MHz
  HSE and prescaler/TSEG values 5/8/1. CAN FD and extended frames are rejected.
- MB1381 design routing: CAN1 connector CN11, pin 1 CAN-H and pin 2 CAN-L. Verify the fitted
  board/harness and appropriate bus termination/reference using the team's wiring design.
- Request identifier `0x7E3`, eight data bytes: `03 22 PID_H PID_L 00 00 00 00`.
- Reply identifier `0x7EB`, eight data bytes: `LEN 62 PID_H PID_L VALUE...` with padding.
  `LEN` is 5 for two-byte values, 4 for one-byte values. Two-byte values are big-endian.
- Nominal request spacing 50 ms; an outstanding request expires after more than 100 ms.
  Each accepted signal becomes stale after more than 2000 ms by default.

| PID | Measurement | Reply value encoding |
| --- | --- | --- |
| F00D | Pack voltage | u16, 0.1 V/count |
| F014 | Summed pack voltage | u16, 0.01 V/count, rounded to 0.1 V internally |
| F015 | Signed pack current | u16 offset binary: (raw - 32767) / 10 A |
| F00F | SOC | u8, 0.5 percent/count, displayed as rounded integer percent |
| F028/F029/F02A | Highest/lowest/average temperature | signed 8-bit whole degrees Celsius |
| F032/F033/F034 | Lowest/highest/average cell voltage | u16, 0.0001 V/count |

Example 400.0 V pack-voltage reply: `0x7EB#0562F00D0FA00000`.
Only these ten battery signals are decoded. Speed, N/D/R, IMD, BSPD, SDC and other auxiliary
inputs have no real provider in this repository; connecting a slave does not supply them
unless their actual CAN/GPIO definitions are implemented. USB alone supplies no measurements.

## What is verified and what remains unknown

The real board displayed the three gauges and the user verified touch navigation to CELLS
and PACK on 2026-11-09. Host tests exercise all ten fields and a slave joining after an
unacknowledged request, using the production decoder behind HAL fakes. This does not test
physical CAN arbitration, ACK, transceiver/harness behavior or the external slave firmware.

No DBC or separate slave firmware has been located in accessible local files, fetched Git
branches, or the searched ZIP archives. The MB1381 BDP is a board-design package, not a DBC.
Compatibility with the eventual slave: **Insufficient data to verify** until its actual
definitions or representative traffic are available and an end-to-end bench test passes.

The first live board boot reported eMMC telemetry ready but an unattached fault journal;
fault entries were unsaved. This separate storage issue remains unresolved and is not a
reason to synthesize missing measurements. Demo bypasses storage entirely.
