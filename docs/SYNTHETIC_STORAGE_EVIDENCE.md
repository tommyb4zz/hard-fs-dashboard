# Synthetic storage and binary export evidence

## Sources researched and purpose

- `Src/app_emmc_log.c`, `Inc/app_emmc_log.h` and `Inc/app_can.h`: establish the existing storage layout, counters, partial flushes, session handling, CRC algorithm and dashboard field units.
- `dump_emmc_bin.py`: compare Python stream CRC accumulation and framing to the bytes produced by the C exporter.
- `Drivers/BSP/STM32H745I-DISCO/stm32h745i_discovery_mmc.h`: match fake MMC function signatures to the checked-in BSP interface.
- Prior ignored reproductions `build/bdp-review/emmc_harness.c`, `check_export.py` and `export-evidence.json`: turn confirmed failures into permanent regressions rather than relying on the prior ad hoc harness.
- [Python 3.13 `binascii.crc32` documentation](https://docs.python.org/3.13/library/binascii.html#binascii.crc32): verify the default zero CRC and the documented use of the previous returned CRC for incremental updates. The C-to-Python test additionally verifies agreement with the actual production stream.

## Reproduction before fixing

The first permanent run used the unchanged logger/export behavior with seven tests. It had one success, two assertion failures and four errors. The key observed failures were:

- Append timestamp 11, flush, append timestamp 22, flush, dump: exported timestamps were `[11, 4294967295]`, rather than `[11, 22]`. The former code allocated a fresh block after each partial flush, while dump indexing assumed all previous blocks were filled with whole-record slots.
- The serial CLI rejected the production stream with `Stream CRC mismatch: expected 0xC654FF0C, got 0xB298157A`. Those exact CRCs are for the permanent reproduction's deliberately recognizable record values, not a universal session checksum.
- Repeated flushes consumed the tiny fake card before all twelve records could fit. The full-block header failure path also lacked a retry once staging was cleared.

The status allocation regression was added after the initial tail fix. It failed when `staged_bytes` still included a retained, already counted tail; it passed after the status accounting adjustment.

## Implemented decisions

`app_emmc_log_flush_locked` keeps a partially filled tail in the existing 512-byte staging buffer. If that tail already has a stored block, dirty records rewrite the same block; `written_blocks` increases only for the first successful data write to a new block. A tail is released when no additional whole record fits. A clean repeated flush does not rewrite the data. A failed data write leaves the dirty buffer available for retry. A successful data write followed by a failed header write leaves a header-dirty flag, so a subsequent flush retries the header even when the full tail has been released.

At capacity, append rejects the next record before advancing its record count or timestamp. The existing overflow flag remains part of the export. `status.staged_bytes` counts a new unallocated staging block only; it is zero for a retained tail already represented by `written_blocks`, including when that retained tail receives another sample. It is an allocation-accounting component, not a complete pending-durability byte count. Internal dirty flags retain the distinction.

No storage header or binary wire version changed. The storage header remains version 1; HFDB/HFDE exports retain their 24-byte header, 42-byte packed records and 16-byte footer. The permanent host test confirms the current C record size of 40 bytes and twelve records per 512-byte block through boundary and allocation assertions. Raw structure layout is still compiler-dependent as before; cross-build format migration was not added.

`dump_emmc_bin.read_dump` accepts a binary file-like source and optional raw-output sink. It checks version, header sizes/CRC, footer count/CRC and the incremental stream CRC; row flags continue to report stored-record and dump-record validity. Python CRC accumulation now starts at zero and uses each returned CRC directly, without the extra final XOR. The existing UART CLI delegates to this reader. The new `--input-bin` mode uses captured fake-eMMC bytes without requiring pyserial or a UART device, rejects aliases of any output path, and rejects trailing bytes after the footer. pyserial is required only when UART capture is selected.

## Permanent verification

Focused command:

```sh
.venv/bin/python -m unittest discover -s tests -p test_storage_export.py -v
```

The focused suite passed all 17 tests. It compiles the production logger with `-Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer`. Its checks cover:

- Binary and direct CSV round trips after repeated partial flushes.
- Flush/dump followed by records 12 and 13; automatic block crossing without caller flushes.
- Capacity rejection without an extra counted record; empty/repeated flushes and session reset.
- Allocation status before and after partial flush/append and full-block release.
- Data-write, header-write, block-read and UART-transmit failure/retry paths.
- Signed current and temperature extrema, all recognizable field values and exact raw capture preservation.
- Stored-record damage reported with its invalid flag while transport CRC remains valid.
- Header, record-stream and footer corruption; footer count mismatch; unsupported versions and truncated captures.
- Offline CLI without pyserial, input preservation, self-overwrite rejection and trailing-data rejection.

Integration with root's dashboard runner and final full-suite/firmware builds is recorded by the root agent after all module changes are joined. A full discovery run during concurrent host scaffolding failed only the dashboard-host compile setup because LVGL types were not visible in its header include order; the storage-focused suite passed. This intermediate result is not the final integration result.

## Remaining requirements

Initialization still chooses the next session identifier and begins an empty session; it does not resume the prior session. This is existing behavior, preserved by this change. Already generated exports containing padding holes cannot be reconstructed from the export alone. Recovering a prior raw device session would require a separately specified scanner and persistence policy.

Power-loss atomicity, raw-header CRC/journaling, persistent session recovery, eMMC DMA/cache coherence, physical device errors and real UART timing remain unimplemented or unverified by this host path. No physical binding, flashing or hardware I/O was performed. Insufficient data to verify those hardware behaviors from the software host tests.
