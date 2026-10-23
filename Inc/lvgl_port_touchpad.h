<<<<<<< HEAD
=======
/**
 * File purpose: Declare registration of the BSP touch controller as an LVGL pointer device. Call after
 * LVGL/display initialization; the implementation configures display geometry and XY
 * swap. No return status is exposed, so BSP initialization failure leaves touch
 * unregistered without failing the GUI task.
 */

>>>>>>> yousef-dashboard
/*
 * lvgl_port_touchpad.h
 *
 *  Created on: 23 Dec 2021
 *      Author: Ahmet Alperen Bulut / github.com/ahmetalperenbulut
 */

#ifndef INC_LVGL_PORT_TOUCHPAD_H_
#define INC_LVGL_PORT_TOUCHPAD_H_

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
<<<<<<< HEAD
=======
/**
 * Initialize the BSP touch controller for LCD dimensions with XY swap and register an LVGL
 * pointer read callback; return immediately if BSP initialization fails.
 *
 * No arguments or return status; select BSP instance zero, LCD pixel dimensions,
 * TS_SWAP_XY and accuracy 5. BSP init failure silently leaves touch unregistered.
 * Otherwise initialize the persistent LVGL driver and store the registration pointer
 * without checking for null; repeated calls have no duplicate-registration guard.
 */
>>>>>>> yousef-dashboard
void touchpad_init(void);


#endif /* INC_LVGL_PORT_TOUCHPAD_H_ */
