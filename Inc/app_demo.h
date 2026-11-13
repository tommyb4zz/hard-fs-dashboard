/* Explicit synthetic inputs for the board demo; never a fallback for missing CAN data. */
#ifndef APP_DEMO_H
#define APP_DEMO_H

#include "app_dashboard_ui.h"

/**
 * Fill a deterministic 40-second synthetic cycle, in the units declared in app_can.h.
 * now_ms is elapsed HAL time; all ten signals get that timestamp. No persistent state,
 * peripheral, CAN traffic or storage is used. Null output pointers leave both untouched.
 */
void app_demo_sample(uint32_t now_ms, app_can_dashboard_data_t *data, app_aux_ui_state_t *aux);

/** Attach a finite RAM-only demo journal once; never initialize or write eMMC. */
void app_demo_history_init(void);
/** Feed synthetic faults/recovery to the production episode monitor using RAM storage. */
void app_demo_observe(const app_can_dashboard_data_t *data,
                      const app_aux_ui_state_t *aux,
                      uint32_t now_ms);

#endif
