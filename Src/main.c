/**
 * File purpose: Start the STM32 dashboard: configure MPU/caches/clocks and debug UART, map
 * QSPI assets, create application tasks and start FreeRTOS. Provide fatal-error and stack-
 * overflow hooks.
 */

#include "main.h"

#include <stdio.h>

#include "app_can.h"
#include "app_tasks.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h745i_discovery_qspi.h"

void SystemClock_Config(void);
static void MPU_Config(void);
static void DebugConsole_Init(void);

#ifdef DEBUG
#  define APP_BUILD_CONFIGURATION_NAME "Debug"
#else
#  define APP_BUILD_CONFIGURATION_NAME "Release"
#endif

/**
 * Start firmware from the reset handler; there are no caller arguments and successful execution
 * stays inside the FreeRTOS scheduler.
 *
 * Configure MPU regions before enabling instruction/data caches, then initialize HAL, clocks
 * and the COM1 debug console. Initialize QSPI in QPI/DTR dual-flash mode and enable memory
 * mapping before any UI code or assets placed at 0x90000000 can be used. Barriers and
 * instruction-cache invalidation follow the mapping change.
 *
 * Print the build/debug settings, wait 50 ms, create application tasks and start scheduling. A
 * QSPI error, task-creation failure or scheduler return enters the nonreturning Error_Handler.
 * This path is not executed by the desktop synthetic runner; mapped flash must be programmed
 * separately on hardware.
 */
int main(void) {
  BSP_QSPI_Init_t qspi_init;

  MPU_Config();
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();

  SystemClock_Config();
  DebugConsole_Init();

  printf("\r\n[boot] hard-fs-dashboard build=%s can_debug=%u\r\n",
         APP_BUILD_CONFIGURATION_NAME,
         app_can_is_debug_enabled() ? 1U : 0U);

  HAL_Delay(50);

  qspi_init.InterfaceMode = MT25TL01G_QPI_MODE;
  qspi_init.TransferRate = MT25TL01G_DTR_TRANSFER;
  qspi_init.DualFlashMode = MT25TL01G_DUALFLASH_ENABLE;

  if (BSP_QSPI_Init(0, &qspi_init) != BSP_ERROR_NONE) {
    Error_Handler();
  }

  if (BSP_QSPI_EnableMemoryMappedMode(0) != BSP_ERROR_NONE) {
    Error_Handler();
  }

  __DSB();
  __ISB();
  SCB_InvalidateICache();

  if (!app_tasks_start()) {
    Error_Handler();
  }

  vTaskStartScheduler();
  Error_Handler();
}

/**
 * Configure the board power and clock tree; takes no parameters and returns only after the
 * checked HAL configuration calls succeed.
 *
 * Select LDO supply, request voltage scale 1 and poll VOSRDY. Unsigned HAL-tick subtraction
 * gives a wrap-tolerant timeout test strictly greater than 100 ms. A supply failure or
 * readiness timeout halts in Error_Handler.
 *
 * Enable HSE and PLL1 with M=5, N=160, P=2, Q=4 and R=2; disable HSI/CSI. With the configured
 * HSE_VALUE of 25 MHz, the requested PLLP/system rate is 400 MHz, HCLK 200 MHz and divided APB
 * clocks 100 MHz. These are configuration calculations, not measured frequencies. HAL
 * oscillator/clock failures halt; the final flash latency is four wait states.
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  uint32_t timeout_start = HAL_GetTick();

  if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK) {
    Error_Handler();
  }

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
    if ((HAL_GetTick() - timeout_start) > 100U) {
      Error_Handler();
    }
  }

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_OFF;
  RCC_OscInitStruct.CSIState = RCC_CSI_OFF;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 160;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
                                RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
    Error_Handler();
  }
}

/**
 * Terminate normal application progress after a fatal initialization/runtime error. No input,
 * status code or recovery is provided.
 *
 * Disable maskable interrupts and execute NOP forever. This preserves a halted state for
 * debugger inspection but does not save fault registers, export logs, reset the MCU or define a
 * physical safe-output policy. Callers cannot continue after this function.
 */
void Error_Handler(void) {
  __disable_irq();
  while (1) {
    __NOP();
  }
}

/**
 * FreeRTOS callback for a detected task-stack overflow. xTask and pcTaskName identify the
 * affected task, but this implementation deliberately discards both.
 *
 * Enter Error_Handler, disabling maskable interrupts and halting permanently. The hook does not
 * print the task name or recover memory; its invocation depends on the configured kernel stack-
 * check mechanism.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  (void)pcTaskName;

  Error_Handler();
}

/**
 * Configure BSP COM1 for diagnostic printf output: 115200 baud, eight data bits, one stop bit,
 * no parity and no hardware flow control.
 *
 * Zero-initialize the local configuration, initialize the BSP port and select it as the BSP log
 * destination. Both BSP return codes are checked and failures enter Error_Handler. This mutates
 * UART/BSP state; it neither configures the binary log-export protocol nor validates a
 * connected serial device.
 */
static void DebugConsole_Init(void) {
  COM_InitTypeDef com_init = {0};

  com_init.BaudRate = 115200U;
  com_init.WordLength = COM_WORDLENGTH_8B;
  com_init.StopBits = COM_STOPBITS_1;
  com_init.Parity = COM_PARITY_NONE;
  com_init.HwFlowCtl = COM_HWCONTROL_NONE;

  if (BSP_COM_Init(COM1, &com_init) != BSP_ERROR_NONE) {
    Error_Handler();
  }

  if (BSP_COM_SelectLogPort(COM1) != BSP_ERROR_NONE) {
    Error_Handler();
  }
}

/**
 * Establish memory-access/cache attributes before caches and application peripherals are
 * enabled. No arguments or return status are exposed.
 *
 * Disable the MPU, reuse one zero-initialized descriptor and configure four numbered full-
 * access, executable, nonshareable, cacheable, nonbufferable regions: AXI SRAM 0x24000000/512
 * KiB; D2 SRAM 0x30000000/256 KiB; SDRAM 0xD0000000/32 MiB; QSPI 0x90000000/128 MiB. Enable the
 * privileged default map afterward.
 *
 * The attributes describe CPU access policy rather than physical capacity. The supplied BDP
 * exposes 8 MiB of SDRAM, so the 32 MiB MPU region is a documented bounds-reconciliation gap;
 * this function does not test memory or initialize its controllers.
 */
static void MPU_Config(void) {
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  HAL_MPU_Disable();

  /* All four regions use these access and cache attributes. The vendor HAL reads this
   * configuration without modifying it, so only region number, base and size change below. */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  /* AXI SRAM. */
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x24000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* D2 SRAM. */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256KB;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* External SDRAM. */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress = 0xD0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32MB;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Memory-mapped QSPI. */
  MPU_InitStruct.Number = MPU_REGION_NUMBER3;
  MPU_InitStruct.BaseAddress = 0x90000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_128MB;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
