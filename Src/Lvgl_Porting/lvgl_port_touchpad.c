<<<<<<< HEAD
=======
/**
 * File purpose: Adapt BSP instance-zero touch samples to LVGL pointer press/release events in
 * display pixel coordinates. Initialization selects LCD dimensions, XY swap and accuracy; reads
 * retain the previous coordinates after release. BSP initialization failure returns
 * silently and read failures/registration failures are not surfaced. This path is
 * separate from synthetic CAN input.
 */

>>>>>>> yousef-dashboard
#include "lvgl_port_touchpad.h"
#include "lvgl/lvgl.h"

#include "stm32h745i_discovery.h"
#include "stm32h745i_discovery_lcd.h"
#include "stm32h745i_discovery_ts.h"

#define TS_INSTANCE (0U)

<<<<<<< HEAD
=======
/* BSP read state and registered driver have static lifetime; failed reads are not cleared. */
>>>>>>> yousef-dashboard
static TS_State_t ts_state;
static lv_indev_drv_t indev_drv;
static lv_indev_t *touch_indev = NULL;

<<<<<<< HEAD
=======
/**
 * LVGL pointer callback: ignore drv, read the BSP touch state and fill data with pressed
 * coordinates or the last position with released state; BSP read status is not checked.
 *
 * LVGL supplies writable data; no null guard is present. TouchX/TouchY are pixel
 * coordinates after BSP orientation; save the last detected pair and reuse it on release
 * so release events keep a position. Ignoring BSP_TS_GetState failure means the existing
 * ts_state may be used after a failed read; no error/unknown event is synthesized.
 */
>>>>>>> yousef-dashboard
static void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  static int16_t last_x = 0;
  static int16_t last_y = 0;

  LV_UNUSED(drv);

  BSP_TS_GetState(TS_INSTANCE, &ts_state);
  if (ts_state.TouchDetected) {
    data->point.x = ts_state.TouchX;
    data->point.y = ts_state.TouchY;
    last_x = data->point.x;
    last_y = data->point.y;
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->point.x = last_x;
    data->point.y = last_y;
    data->state = LV_INDEV_STATE_REL;
  }
}

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
void touchpad_init(void) {
  TS_Init_t ts_init;

  ts_init.Width = LCD_DEFAULT_WIDTH;
  ts_init.Height = LCD_DEFAULT_HEIGHT;
  ts_init.Orientation = TS_SWAP_XY;
  ts_init.Accuracy = 5;

  if (BSP_TS_Init(TS_INSTANCE, &ts_init) != BSP_ERROR_NONE) {
    return;
  }

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read;
  touch_indev = lv_indev_drv_register(&indev_drv);
  LV_UNUSED(touch_indev);
}
