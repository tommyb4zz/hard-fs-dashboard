# Startup refactor review

## Scope and inspected sources

Review only the removal of duplicate declarations/comments and repeated MPU property assignments in `Src/main.c`. Baseline: `build/main-refactor/baseline.c`, saved before this refactor. Source changes and test execution belong to the main agent; this review owns only this report.

- `Inc/main.h` declares `Error_Handler(void)`, and `Src/main.c` includes that header. Its additional local declaration duplicates the header declaration.
- Baseline `MPU_Config` repeats the same eight values for `Enable`, `SubRegionDisable`, `TypeExtField`, `AccessPermission`, `DisableExec`, `IsShareable`, `IsCacheable`, and `IsBufferable` across all four calls. Each region varies only `Number`, `BaseAddress`, and `Size`.
- `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c`, function `HAL_MPU_ConfigRegion`, reads those structure fields to validate parameters and write MPU registers. It does not modify any field of its input structure. Reusing the initialized common fields therefore preserves the configuration values presented to this implementation.
- `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_cortex.h` defines the structure and the region/property constants used here.

## Baseline region configuration

| Order | Number | Base address | Size |
| --- | --- | --- | --- |
| 1 | `MPU_REGION_NUMBER0` | `0x24000000` | `MPU_REGION_SIZE_512KB` |
| 2 | `MPU_REGION_NUMBER1` | `0x30000000` | `MPU_REGION_SIZE_256KB` |
| 3 | `MPU_REGION_NUMBER2` | `0xD0000000` | `MPU_REGION_SIZE_32MB` |
| 4 | `MPU_REGION_NUMBER3` | `0x90000000` | `MPU_REGION_SIZE_128MB` |

Common values: enabled, subregion-disable mask `0x00`, TEX level 0, full access, instruction access enabled, not shareable, cacheable, not bufferable. The baseline calls `HAL_MPU_Disable` before configuration and `HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT)` afterward.

## Independent comparison

No required findings.

- Inspected the baseline-to-current diff. It removes duplicated forward-declaration comments and the local `Error_Handler` declaration, consolidates the eight common MPU assignments, and adds labels for the four retained region blocks. All function-definition comments and the file-purpose header remain.
- An independent read-only Python comparison, using brace-balanced extraction rather than the main agent's parser, confirmed that the bodies of `main`, `SystemClock_Config`, `Error_Handler`, `vApplicationStackOverflowHook`, and `DebugConsole_Init` are byte-for-byte unchanged. Startup ordering, delays, clock values, barriers, and existing error paths in those functions are therefore unchanged by this refactor.
- The same independent comparison accumulated each `MPU_InitStruct` assignment and captured the complete symbolic state at each `HAL_MPU_ConfigRegion` call. All eleven fields at all four calls match the baseline, in the same order. `HAL_MPU_Disable` and `HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT)` remain once each, before and after the region calls respectively.
- Reviewed `build/main-refactor/verify.py` and its `equivalence.json` output. The main agent's verification compiles the exact extracted baseline and current MPU functions using the actual vendor structure and macros under AddressSanitizer/UndefinedBehaviorSanitizer. Their traces match: disable, four complete eleven-field configurations, enable mode 4. A copied first-region address mutation produces a different trace, demonstrating that the comparison detects that change. The trace stub records configuration inputs; it does not access real MPU registers.

## Review assessment and limits

The change removes 24 repeated MPU assignments while preserving explicit region ordering. It adds no abstraction, dependency, allocation, input path, or clock/peripheral setting. Its reuse of common properties is valid for the inspected vendor HAL implementation because that implementation does not mutate its configuration argument.

This report verifies source equivalence and inspects the compiled host trace evidence. Existing tests and firmware build results are recorded by the main agent separately. Physical board execution and hardware compatibility were not evaluated by this review; insufficient data to verify those outcomes.
