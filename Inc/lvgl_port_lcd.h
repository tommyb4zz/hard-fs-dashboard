/**
 * File purpose: Declare board display initialization for the GUI after lv_init and before generated
 * screen creation. The implementation owns two SDRAM buffers and LTDC registration;
 * duplicate calls and critical setup failures enter Error_Handler. The host has its own
 * memory-backed display implementation and does not call this hardware port.
 */

/*
 * lvgl_port_lcd.h
 *
 *  Created on: 23 Dec 2021
 *      Author: Ahmet Alperen Bulut / github.com/ahmetalperenbulut
 */

#ifndef INC_LVGL_PORT_LCD_H_
#define INC_LVGL_PORT_LCD_H_

/*********************
 *      INCLUDES
 *********************/

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Initialize SDRAM, LCD panel and LTDC, clear two full-screen framebuffers and register a
 * full-refresh LVGL display before enabling backlight; duplicate initialization or failed
 * hardware/display setup enters the error handler.
 *
 * Must follow lv_init and precede ui_init. Initialize BSP SDRAM instance zero, prepare
 * controls/clocks/reset/timing, then zero two width*height buffers, maintain cache and
 * register both as a full-screen LVGL draw buffer. display is a persistent registration
 * pointer; a second call is fatal. Backlight is enabled only after nonnull registration.
 * The host simulator uses a different memory display and does not run this routine.
 */
void LCD_init();

/**********************
 *      MACROS
 **********************/

#endif /* INC_LVGL_PORT_LCD_H_ */
