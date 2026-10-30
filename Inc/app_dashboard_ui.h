/**
 * File purpose: Declare pure dashboard-to-LVGL bindings and the explicit auxiliary data model used
 * by firmware demo and host scenarios. Auxiliary speed/mode/statuses include validity so unknown
 * values remain distinguishable from known healthy values. Internal enum names describe display
 * concepts only; no physical GPIO polarity or CAN field binding is defined.
 */
#ifndef APP_DASHBOARD_UI_H
#define APP_DASHBOARD_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "app_can.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal UI order enables the N -> D -> R demo cycle; COUNT is not a valid mode. */
typedef enum {
  APP_DRIVE_MODE_NEUTRAL = 0,
  APP_DRIVE_MODE_DRIVE,
  APP_DRIVE_MODE_REVERSE,
  APP_DRIVE_MODE_COUNT
} app_drive_mode_t;

/* Array order also defines host status-mask bits; labels do not establish wiring/polarity. */
typedef enum {
  APP_STATUS_SIGNAL_IMD = 0,
  APP_STATUS_SIGNAL_FAULT,
  APP_STATUS_SIGNAL_RTD,
  APP_STATUS_SIGNAL_TSAL,
  APP_STATUS_SIGNAL_BSPD,
  APP_STATUS_SIGNAL_SDC,
  APP_STATUS_SIGNAL_READY_TO_DRIVE,
  APP_STATUS_SIGNAL_COUNT
} app_status_signal_id_t;

/*
 * valid=false means unknown; fault_active is consulted for color only when valid=true.
 * wired=true means a provider exists for this input. Unwired inputs still display as
 * unknown, but they are not malfunctions and are never written to the fault history.
 */
typedef struct {
  bool fault_active;
  bool valid;
  bool wired;
} app_status_signal_state_t;

/**
 * Explicit auxiliary display input: integer km/h, internal drive-mode enum, independent
 * validity flags and seven digital states. status_inputs_initialized controls the boot
 * group display in addition to now_ms; one unknown/fault keeps all indicators visible.
 * Inputs can come from the firmware demo or a host timeline; physical binding is deferred.
 * The *_wired flags (and status_signals[].wired) are set by whichever provider supplies the
 * input; production firmware has none yet, so all are false there.
 */
typedef struct {
  uint16_t speed_kph;
  app_drive_mode_t drive_mode;
  bool speed_valid;
  bool drive_mode_valid;
  bool speed_wired;
  bool drive_mode_wired;
  bool status_inputs_initialized;
  app_status_signal_state_t status_signals[APP_STATUS_SIGNAL_COUNT];
} app_aux_ui_state_t;

/**
 * Display the supplied CAN snapshot on the main, cell-table and pack screens using LVGL time
 * for freshness; read log status for the diagnostics status line. Ignore null data.
 *
 * The snapshot uses units declared in app_can.h. Fresh signals show temperature degrees,
 * rounded cell millivolts, integer kW statistics, pack volts and signed amps; missing or
 * stale signals show "--" and hide gauge fills. Storage usage reserves one eMMC header block
 * and counts already-written partial tails once. Speed statistics need auxiliary input and
 * are updated only by app_ui_refresh.
 */
void app_ui_apply_dashboard_data(const app_can_dashboard_data_t *data);
/**
 * Render timestamp-aware measurements, accumulate speed statistics for the cell table and
 * record malfunction episodes through app_fault_monitor. GUI owner only.
 */
void app_ui_refresh(const app_can_dashboard_data_t *data,
                    const app_aux_ui_state_t *aux,
                    uint32_t now_ms);
/**
 * Apply auxiliary values to existing objects, rendering unknown speed/mode as dashes.
 * Unknown digital inputs use neutral colors; show every status during boot or when any
 * input is unknown or faulted, and hide them only when all inputs are known healthy.
 * Ignore null state.
 *
 * state is read-only input, now_ms controls the 3000 ms boot display. Speed uses integer
 * km/h and mode uses the internal enum; absent validity produces dashes and gray mode
 * styling. Status visibility is group-wide: show all seven during boot/uninitialized input
 * or if any status is unknown/faulted; otherwise hide all seven. Within a visible group
 * each indicator is gray for unknown, red for valid fault, green for valid healthy. This
 * changes LVGL objects only and does not read or invert GPIO.
 */
void app_ui_apply_aux_state(const app_aux_ui_state_t *state, uint32_t now_ms);
/**
 * Update state from elapsed milliseconds using synthetic speed and drive-mode cycles;
 * after the boot interval mark status inputs initialized and leave every digital fault
 * inactive. No CAN or GPIO input is read; null state is ignored.
 *
 * now_ms is elapsed milliseconds; unsigned differences drive one speed increment of 3 km/h
 * per qualifying call at 200 ms intervals, bouncing at 0 and 160. Missed intervals are not
 * replayed. Persistent static state is shared across calls; N/D/R advances every 5000 ms
 * after first initialization. At 3000 ms status_inputs_initialized becomes true; all seven
 * statuses are always set valid and nonfaulted, and every demo input is marked wired. This
 * automatic demo has no reset API, is not called by firmware and is bypassed by host inputs.
 */
void app_ui_update_aux_placeholders(app_aux_ui_state_t *state, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* APP_DASHBOARD_UI_H */
