<<<<<<< HEAD
=======
/**
 * File purpose: Implement active Cortex-M exception and board interrupt entry points;
 * route scheduler ticks/context switches to FreeRTOS and eMMC/SDRAM/user-button interrupts
 * to the BSP.
 */

>>>>>>> yousef-dashboard
/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    stm32h7xx_it.c
 * @brief   Interrupt Service Routines.
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h7xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
<<<<<<< HEAD
#include "lvgl/lvgl.h"
=======
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h745i_discovery_mmc.h"
>>>>>>> yousef-dashboard
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
<<<<<<< HEAD
static volatile uint8_t lvgl_tick_enabled = 0U;
=======
>>>>>>> yousef-dashboard

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
<<<<<<< HEAD
void LVGL_TickSetEnabled(uint8_t enabled) {
  lvgl_tick_enabled = enabled;
}
=======
/**
 * FreeRTOS port entry point used by SVC_Handler to service the supervisor-call exception;
 * implemented by the external kernel port.
 */
extern void vPortSVCHandler(void);
/**
 * FreeRTOS port entry point used by PendSV_Handler for context switching; implemented by
 * the external kernel port.
 */
extern void xPortPendSVHandler(void);
/**
 * FreeRTOS port entry point used by SysTick_Handler to advance scheduler timing after
 * startup; implemented by the external kernel port.
 */
extern void xPortSysTickHandler(void);
>>>>>>> yousef-dashboard

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
// extern MDMA_HandleTypeDef hmdma_mdma_channel40_sw_0;
// extern QSPI_HandleTypeDef hqspi;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
 * @brief This function handles Non maskable interrupt.
 */
<<<<<<< HEAD
=======
/**
 * Handle the nonmaskable exception by entering an infinite loop. No arguments, diagnostic
 * capture or recovery are provided; the handler never returns to the interrupted context.
 * Unlike Error_Handler, this body does not explicitly disable maskable interrupts.
 */
>>>>>>> yousef-dashboard
void NMI_Handler(void) {
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
  while (1) {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
 * @brief This function handles Hard fault interrupt.
 */
<<<<<<< HEAD
=======
/**
 * Stop permanently when the Cortex-M hard-fault exception reaches this vector. The handler
 * loops without reading fault-status registers or the stacked program counter and does not
 * return a status or restart the task. A debugger is required to inspect the cause in this
 * implementation.
 */
>>>>>>> yousef-dashboard
void HardFault_Handler(void) {
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1) {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
 * @brief This function handles Memory management fault.
 */
<<<<<<< HEAD
=======
/**
 * Stop permanently for a memory-management exception. The body is an infinite loop, with no
 * address/status-register capture, recovery or caller-visible result. Its definition describes
 * the exception response, not evidence that a particular MPU access failed.
 */
>>>>>>> yousef-dashboard
void MemManage_Handler(void) {
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1) {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
 * @brief This function handles Pre-fetch fault, memory access fault.
 */
<<<<<<< HEAD
=======
/**
 * Stop permanently for a bus-fault exception. The function takes no arguments and never
 * returns; it does not identify the failing bus/address, change peripheral state or attempt to
 * recover the interrupted instruction.
 */
>>>>>>> yousef-dashboard
void BusFault_Handler(void) {
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1) {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
 * @brief This function handles Undefined instruction or illegal state.
 */
<<<<<<< HEAD
=======
/**
 * Stop permanently for a usage-fault exception such as a processor-detected instruction/state
 * problem. Loop without recording the hardware fault flags, reporting a cause or resuming
 * execution; the source does not implement a recovery policy.
 */
>>>>>>> yousef-dashboard
void UsageFault_Handler(void) {
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1) {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
 * @brief This function handles System service call via SWI instruction.
 */
<<<<<<< HEAD
=======
/**
 * Route the Cortex-M supervisor-call vector to vPortSVCHandler from the selected FreeRTOS port.
 * The wrapper takes no C parameters and performs no application work before delegation.
 *
 * The port owns the exception-frame/register semantics and scheduler startup. This is a vector
 * entry wrapper, not a normal task-level call to start or switch the application.
 */
>>>>>>> yousef-dashboard
void SVC_Handler(void) {
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */
<<<<<<< HEAD
=======
  vPortSVCHandler();
>>>>>>> yousef-dashboard

  /* USER CODE END SVCall_IRQn 1 */
}

/**
 * @brief This function handles Debug monitor.
 */
<<<<<<< HEAD
=======
/**
 * Return from the debug-monitor exception without application action. All user sections are
 * empty; no debugger message, peripheral update, status return or fault-recovery mechanism is
 * supplied.
 */
>>>>>>> yousef-dashboard
void DebugMon_Handler(void) {
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
 * @brief This function handles Pendable request for system service.
 */
<<<<<<< HEAD
=======
/**
 * Route the pending-supervisor vector to xPortPendSVHandler. The FreeRTOS port owns context
 * saving/restoring and task-switch decisions; this wrapper adds no state or validation and has
 * no C arguments or result.
 */
>>>>>>> yousef-dashboard
void PendSV_Handler(void) {
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */
<<<<<<< HEAD
=======
  xPortPendSVHandler();
>>>>>>> yousef-dashboard

  /* USER CODE END PendSV_IRQn 1 */
}

/**
 * @brief This function handles System tick timer.
 */
<<<<<<< HEAD
=======
/**
 * Advance the HAL tick on every SysTick interrupt and forward the interrupt to
 * xPortSysTickHandler once the scheduler state differs from taskSCHEDULER_NOT_STARTED.
 *
 * The scheduler-state guard prevents kernel tick handling during HAL/startup delays before
 * scheduling begins. The source also forwards when the scheduler is suspended; the kernel
 * decides the resulting bookkeeping. No synthetic runner tick is delivered through this
 * hardware ISR.
 */
>>>>>>> yousef-dashboard
void SysTick_Handler(void) {
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */
<<<<<<< HEAD
  if (lvgl_tick_enabled != 0U) {
    lv_tick_inc(1);
=======
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    xPortSysTickHandler();
>>>>>>> yousef-dashboard
  }

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32H7xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h7xx.s).                    */
/******************************************************************************/

///**
//  * @brief This function handles EXTI line2 interrupt.
//  */
// void EXTI2_IRQHandler(void)
//{
//  /* USER CODE BEGIN EXTI2_IRQn 0 */
//
//  /* USER CODE END EXTI2_IRQn 0 */
//  HAL_GPIO_EXTI_IRQHandler(LCD_INT_Pin);
//  /* USER CODE BEGIN EXTI2_IRQn 1 */
//
//  /* USER CODE END EXTI2_IRQn 1 */
//}
//
///**
//  * @brief This function handles QUADSPI global interrupt.
//  */
// void QUADSPI_IRQHandler(void)
//{
//  /* USER CODE BEGIN QUADSPI_IRQn 0 */
//
//  /* USER CODE END QUADSPI_IRQn 0 */
//  HAL_QSPI_IRQHandler(&hqspi);
<<<<<<< HEAD
=======

/**
 * Forward the SDMMC1 peripheral interrupt to BSP_MMC_IRQHandler for instance zero. No C
 * arguments or error result are exposed; the BSP/HAL owns completion/error processing. The
 * wrapper does not itself flush a log block or export data.
 */
void SDMMC1_IRQHandler(void) {
  BSP_MMC_IRQHandler(0);
}
>>>>>>> yousef-dashboard
//  /* USER CODE BEGIN QUADSPI_IRQn 1 */
//
//  /* USER CODE END QUADSPI_IRQn 1 */
//}
//
///**
//  * @brief This function handles MDMA global interrupt.
//  */
// void MDMA_IRQHandler(void)
//{
//  /* USER CODE BEGIN MDMA_IRQn 0 */
//
//  /* USER CODE END MDMA_IRQn 0 */
//  HAL_MDMA_IRQHandler(&hmdma_mdma_channel40_sw_0);
//  /* USER CODE BEGIN MDMA_IRQn 1 */
//
//  /* USER CODE END MDMA_IRQn 1 */
//}

/* USER CODE BEGIN 1 */
/**
 * @brief  This function handles External lines 15 to 10 interrupt request.
 * @param  None
 * @retval None
 */
<<<<<<< HEAD
=======
/**
 * Forward the grouped EXTI lines 15 through 10 interrupt to BSP_PB_IRQHandler for BUTTON_USER.
 * The BSP performs button-specific pending/dispatch handling; this wrapper does not inspect
 * every line in the group or request a log export directly.
 */
>>>>>>> yousef-dashboard
void EXTI15_10_IRQHandler(void) {
  BSP_PB_IRQHandler(BUTTON_USER);
}

/**
 * @brief This function handles MDMA global interrupt.
 */
<<<<<<< HEAD
=======
/**
 * Forward the MDMA interrupt to BSP_SDRAM_IRQHandler for instance zero. The BSP-owned handle
 * determines transfer handling; this wrapper neither configures an MDMA transfer nor supplies a
 * status result. Empty user sections have no runtime effect.
 */
>>>>>>> yousef-dashboard
void MDMA_IRQHandler(void) {
  /* USER CODE BEGIN MDMA_IRQn 0 */

  /* USER CODE END MDMA_IRQn 0 */
  BSP_SDRAM_IRQHandler(0);
  /* USER CODE BEGIN MDMA_IRQn 1 */

  /* USER CODE END MDMA_IRQn 1 */
}
/* USER CODE END 1 */
