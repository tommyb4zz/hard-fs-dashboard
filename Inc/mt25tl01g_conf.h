/**
<<<<<<< HEAD
=======
 * Supply QSPI component dummy-cycle settings for standard/quad STR and DTR reads. Most
 * configured counts are eight clocks; the ordinary DTR read setting is six. These are driver
 * protocol constants rather than flash capacity or a JEDEC-ID result. Actual fitted-
 * device/profile compatibility remains a BDP follow-up; inclusion of a legacy component name
 * alone establishes neither compatibility nor failure.
 */

/**
 * File purpose: Configure QSPI read dummy-cycle counts for STR/DTR access through the
 * MT25TL01G flash component driver.
 */

/**
>>>>>>> yousef-dashboard
  ******************************************************************************
  * @file    mt25tl01g_conf_template.h
  * @author  MCD Application Team
  * @brief   This file contains all the description of the
  *          MT25TL01G QSPI memory.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2018 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef MT25TL01G_CONF_H
#define MT25TL01G_CONF_H

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"

/** @addtogroup BSP
  * @{
  */
#define CONF_QSPI_DUMMY_CLOCK                 8U

/* Dummy cycles for STR read mode */
#define MT25TL01G_DUMMY_CYCLES_READ_QUAD      8U
#define MT25TL01G_DUMMY_CYCLES_READ           8U
/* Dummy cycles for DTR read mode */
#define MT25TL01G_DUMMY_CYCLES_READ_DTR       6U
#define MT25TL01G_DUMMY_CYCLES_READ_QUAD_DTR  8U

#ifdef __cplusplus
}
#endif

#endif /* MT25TL01G_CONF_H */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
