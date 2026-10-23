# Production CAN host tests

`test_app_can.c` includes the actual `Src/app_can.c` and `Src/app_can_config.c` against a minimal in-memory HAL boundary. The checked-in STM32 HAL source determines DLC copy lengths. Frame validation, conversion, statistics and request scheduling execute production C code.

```sh
cmake -S tests/can_host -B build/can-host -DCMAKE_C_COMPILER=clang
cmake --build build/can-host
ctest --test-dir build/can-host --output-on-failure
```

AddressSanitizer and UndefinedBehaviorSanitizer are enabled by default on Clang/GCC. The suite contains 15 independent groups and a separate 64-byte receive-bounds regression check. It covers initialization/retry, nulls, all ten PIDs, scalar wire boundaries, malformed metadata/PCI/DLC, signed current/temperature, SOC clamping, power rounding, statistics, requests, timeout boundaries, tick rollover, FIFO pressure and receive/transmit errors.

The test-only replay adapter accepts stdin lines:

```text
tick_ms identifier_hex is_extended is_remote is_fd payload_hex
0 7eb 0 0 0 0562f00d0fa00000
```

Run `build/can-host/can_host_tests --replay`. Each line injects a frame and updates the fake millisecond clock; the runner prints one JSON dashboard snapshot with an `updated` boolean. Driver state and statistics persist across lines. Payload lengths must have a CAN DLC encoding (0–8, 12, 16, 20, 24, 32, 48 or 64); `-` represents an empty payload. This adapter performs no physical I/O.

`tests/test_can_pipeline.py` receives frames through `python-can` virtual endpoints before feeding them to this adapter. Its timeline test sends requests to the scenario responder and verifies production C output across ramp, pause and malformed-response windows.

The fake HAL is a test boundary, not an STM32H750 board emulator. Controller registers, arbitration, electrical transceiver behavior and application peripherals are unimplemented in these tests.
