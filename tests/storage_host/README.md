# Production eMMC host regressions

Run from the repository root:

```sh
.venv/bin/python -m unittest discover -s tests -p test_storage_export.py -v
```

The Python suite compiles `test_app_emmc_log.c` with the host C compiler, AddressSanitizer and UndefinedBehaviorSanitizer. That adapter includes the production `Src/app_emmc_log.c`; only the FreeRTOS mutex, MMC BSP and UART transport are replaced by single-threaded RAM-backed fakes. Assertions bound every fake block access and capture write. No MCU, storage card, serial device or physical CAN bus is accessed.

The tests use real production binary and CSV exports. They cover repeated partial flushes, a dump followed by append, automatic whole-record block boundaries, capacity rejection, status allocation counters, session reset, injected data/header/read/UART failures, stored-record corruption and Python framing/count/CRC validation. The fake uses a deliberately small card to exercise capacity without allocating the firmware's full reserved region.

The target's current in-memory records occupy 40 bytes, with twelve records per 512-byte block. A partial flush retains its tail buffer and later rewrites that block. The status `staged_bytes` reports bytes in a new, unallocated block; it excludes a retained tail that is already covered by `written_blocks`. Private dirty flags identify pending data/header writes. A host result is evidence for these software paths, not eMMC timing, power-loss recovery, DMA/cache coherence or FreeRTOS scheduling.
