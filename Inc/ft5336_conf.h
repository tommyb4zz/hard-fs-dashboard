/**
 * Configure the FT5336 component driver: automatic calibration is disabled and logical X/Y
 * limits are 480/272. These macros are read by the touch component, while the application port
 * supplies orientation and LVGL pointer registration. The header declares no callable code and
 * does not check that a fitted controller or coordinate transform works.
 */

/**
 * File purpose: Configure FT5336 touch-component auto-calibration and maximum X/Y
 * coordinate lengths.
 */

/**
  ******************************************************************************
  * @file    ft5336_conf.h
  * @author  MCD Application Team
  * @brief   This file contains specific configuration for the
  *          ft5336.c that can be modified by user.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef FT5336_CONF_H
#define FT5336_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
/* Macros --------------------------------------------------------------------*/
/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
#define FT5336_AUTO_CALIBRATION_ENABLED      0U
#define FT5336_MAX_X_LENGTH                  480U
#define FT5336_MAX_Y_LENGTH                  272U
  
#ifdef __cplusplus
}
#endif
#endif /* FT5336_CONF_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
