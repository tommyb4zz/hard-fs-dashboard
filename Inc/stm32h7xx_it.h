/**
 * Declare the Cortex-M exception and board-peripheral vector entry symbols. Definitions in
 * Src/stm32h7xx_it.c route kernel/MMC/button/SDRAM interrupts or halt faults. A declaration by
 * itself does not enable the interrupt or provide a strong definition; unimplemented vector
 * names may retain the startup's weak Default_Handler alias.
 */

/**
 * File purpose: Declare Cortex-M exception and board interrupt entry points; some retained
 * peripheral declarations use the startup default handler in this build.
 */

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7xx_it.h
  * @brief   This file contains the headers of the interrupt handlers.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
 ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __STM32H7xx_IT_H
#define __STM32H7xx_IT_H

#ifdef __cplusplus
 extern "C" {
#endif

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
/**
 * Handle the nonmaskable exception by entering an infinite loop. No arguments, diagnostic
 * capture or recovery are provided; the handler never returns to the interrupted context.
 * Unlike Error_Handler, this body does not explicitly disable maskable interrupts.
 */
void NMI_Handler(void);
/**
 * Stop permanently when the Cortex-M hard-fault exception reaches this vector. The handler
 * loops without reading fault-status registers or the stacked program counter and does not
 * return a status or restart the task. A debugger is required to inspect the cause in this
 * implementation.
 */
void HardFault_Handler(void);
/**
 * Stop permanently for a memory-management exception. The body is an infinite loop, with no
 * address/status-register capture, recovery or caller-visible result. Its definition describes
 * the exception response, not evidence that a particular MPU access failed.
 */
void MemManage_Handler(void);
/**
 * Stop permanently for a bus-fault exception. The function takes no arguments and never
 * returns; it does not identify the failing bus/address, change peripheral state or attempt to
 * recover the interrupted instruction.
 */
void BusFault_Handler(void);
/**
 * Stop permanently for a usage-fault exception such as a processor-detected instruction/state
 * problem. Loop without recording the hardware fault flags, reporting a cause or resuming
 * execution; the source does not implement a recovery policy.
 */
void UsageFault_Handler(void);
/**
 * Route the Cortex-M supervisor-call vector to vPortSVCHandler from the selected FreeRTOS port.
 * The wrapper takes no C parameters and performs no application work before delegation.
 *
 * The port owns the exception-frame/register semantics and scheduler startup. This is a vector
 * entry wrapper, not a normal task-level call to start or switch the application.
 */
void SVC_Handler(void);
/**
 * Return from the debug-monitor exception without application action. All user sections are
 * empty; no debugger message, peripheral update, status return or fault-recovery mechanism is
 * supplied.
 */
void DebugMon_Handler(void);
/**
 * Route the pending-supervisor vector to xPortPendSVHandler. The FreeRTOS port owns context
 * saving/restoring and task-switch decisions; this wrapper adds no state or validation and has
 * no C arguments or result.
 */
void PendSV_Handler(void);
/**
 * Advance the HAL tick on every SysTick interrupt and forward the interrupt to
 * xPortSysTickHandler once the scheduler state differs from taskSCHEDULER_NOT_STARTED.
 *
 * The scheduler-state guard prevents kernel tick handling during HAL/startup delays before
 * scheduling begins. The source also forwards when the scheduler is suspended; the kernel
 * decides the resulting bookkeeping. No synthetic runner tick is delivered through this
 * hardware ISR.
 */
void SysTick_Handler(void);
/**
 * Retained external-line-2 handler declaration; the project C implementation is commented
 * out and the startup weak alias falls back to Default_Handler.
 */
void EXTI2_IRQHandler(void);
/**
 * Forward the grouped EXTI lines 15 through 10 interrupt to BSP_PB_IRQHandler for BUTTON_USER.
 * The BSP performs button-specific pending/dispatch handling; this wrapper does not inspect
 * every line in the group or request a log export directly.
 */
void EXTI15_10_IRQHandler(void);
/**
 * Retained QSPI handler declaration; the project C implementation is commented out and the
 * startup weak alias falls back to Default_Handler.
 */
void QUADSPI_IRQHandler(void);
/**
 * Forward the MDMA interrupt to BSP_SDRAM_IRQHandler for instance zero. The BSP-owned handle
 * determines transfer handling; this wrapper neither configures an MDMA transfer nor supplies a
 * status result. Empty user sections have no runtime effect.
 */
void MDMA_IRQHandler(void);
/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* __STM32H7xx_IT_H */
