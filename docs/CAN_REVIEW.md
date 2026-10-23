# Independent CAN driver and host-test review

Review date: 2026-10-10. Scope: the receive-validation/buffer changes in `Src/app_can.c`, the production-C host harness in `tests/can_host/`, and the subsequent test-only stdin replay adapter. The reviewer did not edit production or test sources.

## Verdict and resolved findings

No required findings remain in the reviewed change after the following corrections by the root agent.

- **Resolved — host HAL constant fidelity:** `tests/can_host/include/stm32h7xx_hal.h:30`, `:31`, `:34` and `:66` initially used placeholder values for `RCC_PERIPHCLK_FDCAN`, `RCC_FDCANCLKSOURCE_HSE`, `FDCAN_DATA_BYTES_8` and `FDCAN_RX_FIFO0`. Their verified vendor values are now `0x8000`, `0`, `4` and `0x40`, respectively. Comparison against `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_fdcan.h` and `stm32h7xx_hal_rcc_ex.h` checked all 37 numeric FDCAN/RCC fake constants and found zero remaining mismatches. Standard/extended ID, data/remote frame, classic/FD format, BRS and every DLC value match the checked-in vendor header.
- **Resolved — optional isolation cleanup:** `reset_state` now clears the captured standard/global filters as well as all mutable production driver singleton state, fake timing/error/FIFO counters and captured transmit data. Existing queued frame bytes are not cleared, but reset queue indices prevent their consumption and each enqueue overwrites the complete frame before it is read.

## Source-backed assessment

- **Correctness and boundary validation:** `Src/app_can.c:604` rejects incorrect response IDs, extended identifiers, remote frames and FD format before measurement/statistics writes. The DLC comparison at `:611` requires eight bytes. The helper at `:583` compares the complete PCI byte with the existing per-PID length table, rejecting all other values, including non-single-frame high-nibble types and unknown PIDs. A service check precedes field decoding. Tests exercise all 256 PCI byte values for all ten PIDs, metadata rejection, unknown messages and every DLC code plus one invalid code.
- **Receive memory bounds:** The vendor HAL extracts a four-bit DLC at `stm32h7xx_hal_fdcan.c:3084` and copies `DLCtoBytes[DLC]` bytes at `:3103`; its table at `:255` reaches 64 bytes. The fake uses the same 16-entry table and copies that length before decoder validation. The production receive buffer at `Src/app_can.c:786` now holds 64 bytes, protecting the application buffer even when a rejected header causes the HAL to copy the maximum payload. The independent mutation below proves the regression test detects the former eight-byte buffer.
- **Signed/scalar behavior:** `test_exhaustive_scalar_domains` checks all 65,536 raw current values and all 256 SOC/average-temperature values. Other groups check all ten PID destinations, signed current/power statistics, positive/negative half rounding, voltage fallback, saturation and temperature endpoints. These tests verify the repository's implemented units/offsets; they do not establish compatibility with an unidentified physical BMS.
- **Request timing and state:** Request-cycle tests verify the ten-PID order, request identifier/service/payload, 50 ms gate and sequence wrap. The timeout checks hold the pending request at exactly 100 ms and advance at 101 ms. The rollover group crosses `UINT32_MAX` with the same unsigned subtraction used by the driver. TX backpressure/failure leaves the request available for retry; RX failure terminates the poll and allows subsequent FIFO recovery. Valid unsolicited telemetry does not clear a different outstanding PID, and rejected matching-PID telemetry leaves it pending.
- **Readability and architecture:** The exact-length helper uses the existing request table rather than duplicating protocol length rules. The host harness includes the production implementation and confines fake hardware behavior to HAL boundaries. No additional production dependency was introduced by the receive fix.
- **Security and performance:** Null guards and fixed payload bounds precede decode reads; rejected metadata does not mutate telemetry/statistics. The exact-length lookup scans the fixed ten-entry table. The production FIFO-drain loop is pre-existing; this review does not claim a measured hardware scheduling/throughput result.
- **Replay adapter:** The test-only stdin adapter checks numeric bounds, flags, hex digits, even byte strings and supported CAN payload lengths before filling its 64-byte frame array. Independent replay checks observed a valid 4000-decivolt update, rejected an FD reply without changing that value, then accepted a valid 4200-decivolt reply. Six malformed records (invalid hex, odd hex length, unsupported nine-byte payload, tick overflow, identifier overflow and invalid metadata flag) exited with code 2 under the sanitizer build.

## Independent verification

The requested command using `build/can-host` and `-G Ninja` encountered the existing `Unix Makefiles` cache. An isolated directory retained that build state and used the same source, compiler and sanitizer configuration:

```sh
.venv/bin/cmake -S tests/can_host -B build/can-host-review -G Ninja -DCMAKE_C_COMPILER=/usr/bin/clang
.venv/bin/cmake --build build/can-host-review
.venv/bin/ctest --test-dir build/can-host-review --output-on-failure
build/can-host-review/can_host_tests
```

Result: AppleClang 17.0.0; AddressSanitizer and UndefinedBehaviorSanitizer enabled by the host CMake target. Both CTest targets passed. The direct suite reported **15 groups, 69,431 assertions, zero failures**. The final rebuild/CTest run after the constant and reset-capture corrections also passed both targets.

## Independent mutation experiment

The reviewer copied the driver/config sources, their headers and the complete host harness into `build/can-review-mutation/`, retaining their relative layout. Only the copied driver changed: `APP_CAN_MAX_RX_PAYLOAD_BYTES` was reduced from 64 to 8. The shared source files were not modified.

```sh
.venv/bin/cmake -S build/can-review-mutation/tests/can_host -B build/can-review-mutation-output -G Ninja -DCMAKE_C_COMPILER=/usr/bin/clang
.venv/bin/cmake --build build/can-review-mutation-output
.venv/bin/ctest --test-dir build/can-review-mutation-output -R can_driver_payload_bounds --output-on-failure
```

Result: the payload-bounds target failed with CTest exit code 8. AddressSanitizer reported **stack-buffer-overflow, WRITE of size 64** in the fake `HAL_FDCAN_GetRxMessage`, identifying the copied driver's eight-byte `rx_data` buffer in `app_can_poll_dashboard_data`. SHA-256 comparisons of the eight shared source/header/test files before and after the mutation reported no changes. The mutation stays inside ignored build artifacts.

This evidence covers the reviewed source contract and host HAL boundary. It does not verify physical transceiver wiring, termination, electrical compatibility or real-board CAN traffic.
