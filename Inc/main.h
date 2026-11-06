/**
 * Share HAL/BSP types, board GPIO aliases and fatal-error declarations for firmware sources.
 * Pin macros describe the selected board profile; they do not configure GPIO hardware when this
 * header is included. Error_Handler is implemented in main.c. The retained HAL_TIM_MspPostInit
 * declaration does not establish an active timer implementation in this build.
 */

/**
 * File purpose: Share STM32 HAL/BSP includes, board GPIO definitions and application
 * fatal-error declarations; retain the Cube timer-post-init declaration.
 */

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stm32h745i_discovery.h"
#include "stm32h745i_discovery_lcd.h"
#include "stm32h745i_discovery_sdram.h"
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

/**
 * Retained Cube timer post-initialization declaration; the project example definition is
 * commented out, so this declaration has no active implementation in
 * Src/stm32h7xx_hal_msp.c.
 */
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
/**
 * Terminate normal application progress after a fatal initialization/runtime error. No input,
 * status code or recovery is provided.
 *
 * Disable maskable interrupts and execute NOP forever. This preserves a halted state for
 * debugger inspection but does not save fault registers, export logs, reset the MCU or define a
 * physical safe-output policy. Callers cannot continue after this function.
 */
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define OSC32_IN_Pin GPIO_PIN_14
#define OSC32_IN_GPIO_Port GPIOC
#define LCD_DISPLAY_MODE_Pin GPIO_PIN_7
#define LCD_DISPLAY_MODE_GPIO_Port GPIOD
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define LD2_Pin GPIO_PIN_13
#define LD2_GPIO_Port GPIOI
#define LCD_INT_Pin GPIO_PIN_2
#define LCD_INT_GPIO_Port GPIOG
#define LCD_INT_EXTI_IRQn EXTI2_IRQn
#define OSC_IN_Pin GPIO_PIN_0
#define OSC_IN_GPIO_Port GPIOH
#define LCD_BL_CTRL_Pin GPIO_PIN_0
#define LCD_BL_CTRL_GPIO_Port GPIOK
#define VCP_TX_Pin GPIO_PIN_10
#define VCP_TX_GPIO_Port GPIOB
#define VCP_RX_Pin GPIO_PIN_11
#define VCP_RX_GPIO_Port GPIOB
#define LD1_Pin GPIO_PIN_2
#define LD1_GPIO_Port GPIOJ
#define LCD_RESET_Pin GPIO_PIN_12
#define LCD_RESET_GPIO_Port GPIOB
/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
