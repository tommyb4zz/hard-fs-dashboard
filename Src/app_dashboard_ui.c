/**
 * File purpose: Bind integer-scaled battery snapshots and explicit auxiliary validity/fault state
 * to the existing generated LVGL objects. Format labels, derive bar values and logger occupancy,
 * and control group-wide status visibility. The separate automatic auxiliary generator is retained
 * only for explicit demos/tests; production inputs start unknown. All LVGL mutations require
 * initialized screens and serialized caller access.
 */
#include "app_dashboard_ui.h"

#include "app_data_log.h"
#include "app_emmc_log.h"
#include "app_fault_history.h"
#include "lvgl/lvgl.h"
#include "ui/screens/ui_MainScreen.h"
#include "ui/ui.h"

#define APP_AUX_CAN_PLACEHOLDER_UPDATE_MS 200U
#define APP_DRIVE_MODE_PLACEHOLDER_STEP_MS 5000U
#define APP_STATUS_BOOT_DISPLAY_MS 3000U
#define APP_STATUS_OK_COLOR 0x52BA00U
#define APP_STATUS_FAULT_COLOR 0xF40D0DU
#define APP_STATUS_UNKNOWN_COLOR 0x808080U

/**
 * Return the magnitude of value for current-to-display conversion; the callers pass
 * int16_t current values, so INT32_MIN is outside their input range.
 *
 * Return a signed 32-bit magnitude with no state changes. Widening int16_t before the call
 * preserves its most-negative value; no generic INT32_MIN guard exists.
 */
static int32_t app_ui_abs_int32(int32_t value) {
  return (value < 0) ? -value : value;
}

/**
 * Convert signed Celsius to the UI slider range 1 through 100 by clamping; return the
 * display value without changing the sample.
 *
 * Input units are signed whole degrees Celsius. Negative, zero and very cold samples all
 * display at the minimum, and samples above 100 display at the maximum; this display clamp
 * does not diagnose temperature faults.
 */
static int32_t app_ui_slider_from_temperature(int8_t temperature_c) {
  if (temperature_c < 1) {
    return 1;
  }

  if (temperature_c > 100) {
    return 100;
  }

  return temperature_c;
} /* Fault transitions are retained in the append-only malfunction journal. */

/**
 * If label exists, convert voltage from 100-microvolt units to rounded millivolts and
 * replace its LVGL text; null labels are ignored.
 *
 * Input units are 100 microvolts per count; adding five before division by ten rounds to
 * millivolts. The decimal label contains no units suffix. Only the LVGL object text
 * changes; the integer sample is preserved.
 */
static void app_ui_set_voltage_mv_label(lv_obj_t *label, uint16_t voltage_100uv) {
  uint16_t millivolts = 0U;

  if (label == NULL) {
    return;
  }

  millivolts = (uint16_t)((voltage_100uv + 5U) / 10U);
  lv_label_set_text_fmt(label, "%u", (unsigned int)millivolts);
}

/**
 * Replace an existing LVGL label with the signed decimal value; ignore a null label.
 *
 * The caller determines units (currently integer kW statistics); this helper formats
 * signed base-ten text and has no range conversion. Only an existing object is touched and
 * null is a no-op.
 */
static void app_ui_set_signed_integer_label(lv_obj_t *label, int16_t value) {
  if (label == NULL) {
    return;
  }

  lv_label_set_text_fmt(label, "%d", (int)value);
}

/**
 * Return used/total as a rounded integer percentage, or zero for zero capacity; the 64-bit
 * intermediate arithmetic prevents multiplication overflow.
 *
 * used and total must use the same units (records or bytes). Add half of total for
 * nearest-integer rounding before division; return is not clamped at 100 if used exceeds
 * total. The helper does not read logger state itself.
 */
static uint32_t app_ui_calculate_usage_percent(uint32_t used, uint32_t total) {
  if (total == 0U) {
    return 0U;
  }

  return (uint32_t)(((uint64_t)used * 100U + (total / 2U)) / total);
}

/**
 * Return the LVGL panel pointer mapped to signal_id, or NULL for the count sentinel or an
 * unknown signal; do not create objects.
 *
 * The seven switch arms preserve the generated screen variable names, including
 * ui_panleReadyToDrive. The returned object is borrowed from ui_init and must not be freed
 * here; the mapping has no side effects.
 */
static lv_obj_t *app_ui_get_status_panel(app_status_signal_id_t signal_id) {
  switch (signal_id) {
  case APP_STATUS_SIGNAL_IMD:
    return ui_panelIMD;
  case APP_STATUS_SIGNAL_FAULT:
    return ui_panelFault;
  case APP_STATUS_SIGNAL_RTD:
    return ui_panelRTD;
  case APP_STATUS_SIGNAL_TSAL:
    return ui_panelTSAL;
  case APP_STATUS_SIGNAL_BSPD:
    return ui_panelBSPD;
  case APP_STATUS_SIGNAL_SDC:
    return ui_panelSDC;
  case APP_STATUS_SIGNAL_READY_TO_DRIVE:
    return ui_panleReadyToDrive;
  case APP_STATUS_SIGNAL_COUNT:
  default:
    return NULL;
  }
}

/**
 * Return the LVGL label pointer mapped to signal_id, or NULL for the count sentinel or an
 * unknown signal; do not create objects.
 *
 * Use the same enum order as status_signals and map each entry to its generated LVGL
 * label. Objects are borrowed and may still be null before screen creation; invalid enum
 * values return null.
 */
static lv_obj_t *app_ui_get_status_label(app_status_signal_id_t signal_id) {
  switch (signal_id) {
  case APP_STATUS_SIGNAL_IMD:
    return ui_labelIMD;
  case APP_STATUS_SIGNAL_FAULT:
    return ui_labelFault;
  case APP_STATUS_SIGNAL_RTD:
    return ui_labelRTD;
  case APP_STATUS_SIGNAL_TSAL:
    return ui_labelTSAL;
  case APP_STATUS_SIGNAL_BSPD:
    return ui_labelBSPD;
  case APP_STATUS_SIGNAL_SDC:
    return ui_labelSDC;
  case APP_STATUS_SIGNAL_READY_TO_DRIVE:
    return ui_labelReadyToDrive;
  case APP_STATUS_SIGNAL_COUNT:
  default:
    return NULL;
  }
}

/**
 * Set or clear the LVGL hidden flag according to hidden; ignore a null object.
 *
 * hidden changes only LV_OBJ_FLAG_HIDDEN on the borrowed object; color and text remain
 * unchanged. A null pointer is safe, and this function allocates no objects.
 */
static void app_ui_set_object_hidden(lv_obj_t *obj, bool hidden) {
  if (obj == NULL) {
    return;
  }

  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

/**
 * Apply color to the selected signal panel border and label text when those objects exist.
 *
 * signal_id selects both objects and color is an LVGL color value, not a raw hex integer.
 * Set border/text color for LV_PART_MAIN and default state; absent panel and label are
 * checked separately.
 */
static void app_ui_set_status_indicator_color(app_status_signal_id_t signal_id, lv_color_t color) {
  lv_obj_t *panel = app_ui_get_status_panel(signal_id);
  lv_obj_t *label = app_ui_get_status_label(signal_id);

  if (panel != NULL) {
    lv_obj_set_style_border_color(panel, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  if (label != NULL) {
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
}

/**
 * Return the static N, D or R label for a drive mode, or a dash for an unknown mode.
 *
 * drive_mode is the internal enum, with COUNT only a sentinel. Return a borrowed string
 * literal requiring no allocation or freeing; no CAN message or physical selector is
 * interpreted.
 */
static const char *app_ui_drive_mode_to_text(app_drive_mode_t drive_mode) {
  switch (drive_mode) {
  case APP_DRIVE_MODE_NEUTRAL:
    return "N";
  case APP_DRIVE_MODE_DRIVE:
    return "D";
  case APP_DRIVE_MODE_REVERSE:
    return "R";
  case APP_DRIVE_MODE_COUNT:
  default:
    return "-";
  }
}

/**
 * Update state from elapsed milliseconds using synthetic speed and drive-mode cycles;
 * after the boot interval mark status inputs initialized and leave every digital fault
 * inactive. No CAN or GPIO input is read; null state is ignored.
 *
 * now_ms is elapsed milliseconds; unsigned differences drive one speed increment of 3 km/h
 * per qualifying call at 200 ms intervals, bouncing at 0 and 160. Missed intervals are not
 * replayed. Persistent static state is shared across calls; N/D/R advances every 5000 ms
 * after first initialization. At 3000 ms status_inputs_initialized becomes true; all seven
 * statuses are always set valid and nonfaulted. This automatic firmware demo has no reset
 * API and is bypassed by explicit host inputs.
 */
void app_ui_update_aux_placeholders(app_aux_ui_state_t *state, uint32_t now_ms) {
  static uint32_t last_speed_update_ms = 0U;
  static uint32_t last_drive_mode_update_ms = 0U;
  static int32_t speed_value = 0;
  static int32_t speed_direction = 1;

  if (state == NULL) {
    return;
  }

  if ((now_ms - last_speed_update_ms) >= APP_AUX_CAN_PLACEHOLDER_UPDATE_MS) {
    speed_value += (speed_direction * 3);
    if (speed_value >= 160) {
      speed_value = 160;
      speed_direction = -1;
    } else if (speed_value <= 0) {
      speed_value = 0;
      speed_direction = 1;
    }

    state->speed_kph = (uint16_t)speed_value;
    state->speed_valid = true;
    last_speed_update_ms = now_ms;
  }

  if (!state->drive_mode_valid) {
    state->drive_mode = APP_DRIVE_MODE_NEUTRAL;
    state->drive_mode_valid = true;
    last_drive_mode_update_ms = now_ms;
  } else if ((now_ms - last_drive_mode_update_ms) >= APP_DRIVE_MODE_PLACEHOLDER_STEP_MS) {
    state->drive_mode =
        (app_drive_mode_t)(((uint32_t)state->drive_mode + 1U) % (uint32_t)APP_DRIVE_MODE_COUNT);
    last_drive_mode_update_ms = now_ms;
  }

  /*
   * This explicit demo marks every synthetic digital status known and healthy.
   * Physical input reads and polarity rules are deferred; host scenarios supply
   * their own validity and fault values directly to app_ui_apply_aux_state.
   */
  state->status_inputs_initialized = (now_ms >= APP_STATUS_BOOT_DISPLAY_MS);
  for (uint32_t i = 0U; i < (uint32_t)APP_STATUS_SIGNAL_COUNT; ++i) {
    state->status_signals[i].fault_active = false;
    state->status_signals[i].valid = true;
  }
}

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
void app_ui_apply_aux_state(const app_aux_ui_state_t *state, uint32_t now_ms) {
  bool show_status_indicators = false;

  if (state == NULL) {
    return;
  }

  if (ui_labelSpeed != NULL) {
    if (state->speed_valid) {
      lv_label_set_text_fmt(ui_labelSpeed, "%u", (unsigned int)state->speed_kph);
    } else {
      lv_label_set_text(ui_labelSpeed, "-");
    }
  }

  if (ui_labelDriveMode != NULL) {
    lv_label_set_text(ui_labelDriveMode,
                      state->drive_mode_valid ? app_ui_drive_mode_to_text(state->drive_mode) : "-");
  }

  if (ui_panelDriveMode != NULL) {
    lv_obj_set_style_border_color(
        ui_panelDriveMode,
        lv_color_hex(state->drive_mode_valid && state->drive_mode < APP_DRIVE_MODE_COUNT
                         ? APP_STATUS_OK_COLOR
                         : APP_STATUS_UNKNOWN_COLOR),
        LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  if (ui_labelDriveMode != NULL) {
    lv_obj_set_style_text_color(
        ui_labelDriveMode,
        lv_color_hex(state->drive_mode_valid && state->drive_mode < APP_DRIVE_MODE_COUNT
                         ? APP_STATUS_OK_COLOR
                         : APP_STATUS_UNKNOWN_COLOR),
        LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  if (!state->status_inputs_initialized || (now_ms < APP_STATUS_BOOT_DISPLAY_MS)) {
    show_status_indicators = true;
  } else {
    for (uint32_t i = 0U; i < (uint32_t)APP_STATUS_SIGNAL_COUNT; ++i) {
      if (!state->status_signals[i].valid || state->status_signals[i].fault_active) {
        show_status_indicators = true;
        break;
      }
    }
  }

  for (uint32_t i = 0U; i < (uint32_t)APP_STATUS_SIGNAL_COUNT; ++i) {
    lv_color_t color = !state->status_signals[i].valid ? lv_color_hex(APP_STATUS_UNKNOWN_COLOR)
                                                       : (state->status_signals[i].fault_active
                                                              ? lv_color_hex(APP_STATUS_FAULT_COLOR)
                                                              : lv_color_hex(APP_STATUS_OK_COLOR));
    app_status_signal_id_t signal_id = (app_status_signal_id_t)i;

    app_ui_set_status_indicator_color(signal_id, color);
    app_ui_set_object_hidden(app_ui_get_status_panel(signal_id), !show_status_indicators);
    app_ui_set_object_hidden(app_ui_get_status_label(signal_id), !show_status_indicators);
  }
}

/**
 * Display the supplied CAN snapshot as battery labels and sliders, and show RAM/eMMC log
 * usage in debug labels; read log status but do not mutate the sample. Ignore null data.
 *
 * The snapshot uses units declared in app_can.h. Display temperature degrees, rounded cell
 * millivolts and integer kW statistics, then set temperature/current/SOC sliders. Read
 * independent RAM count/capacity and mutex-protected eMMC status for debug percentages;
 * reserve one eMMC header block and count already-written partial tails once. Null input
 * returns before logger reads. No battery freshness/validity logic is applied, so zero
 * initial and retained old values render as measurements.
 */
/** Test validity independently for each measurement; zero is a valid received value. */
static bool ui_fresh(const app_can_dashboard_data_t *data, unsigned signal, uint32_t now) {
  return (data->valid_mask & (1U << signal)) != 0U &&
         (uint32_t)(now - data->signal_tick_ms[signal]) <= APP_CAN_STALE_MS;
}
/** Format signed current to one decimal amp without discarding its sign. */
static void ui_current(lv_obj_t *label, int16_t current) {
  int32_t magnitude = app_ui_abs_int32(current);
  lv_obj_set_style_text_font(label, &ui_font_TomorrowBoldItalic18, 0);
  lv_label_set_text_fmt(
      label, "%s%ld.%ld", current < 0 ? "-" : "", (long)(magnitude / 10), (long)(magnitude % 10));
}
/** Hide an unknown gauge's fill and mask; leave the existing outline visible. */
static void ui_gauge_valid(lv_obj_t *fill, lv_obj_t *knob, bool fresh) {
  app_ui_set_object_hidden(fill, !fresh);
  app_ui_set_object_hidden(knob, !fresh);
}
/** Render values and diagnostics with explicit age, leaving layout independent of data. */
static void app_ui_apply_at(const app_can_dashboard_data_t *data, uint32_t now) {
  if (data == NULL)
    return;
  const bool fresh[10] = {ui_fresh(data, 0, now),
                          ui_fresh(data, 1, now),
                          ui_fresh(data, 2, now),
                          ui_fresh(data, 3, now),
                          ui_fresh(data, 4, now),
                          ui_fresh(data, 5, now),
                          ui_fresh(data, 6, now),
                          ui_fresh(data, 7, now),
                          ui_fresh(data, 8, now),
                          ui_fresh(data, 9, now)};
  lv_obj_t *temperatures[] = {
      ui_labelBatteryTempHigh, ui_labelBatteryTempAvg, ui_labelBatteryTempLow};
  const int8_t temperature[] = {
      data->highest_temperature_c, data->average_temperature_c, data->lowest_temperature_c};
  const unsigned temp_signals[] = {4, 6, 5};
  lv_obj_t *cells[] = {ui_labelBatteryVoltHigh, ui_labelBatteryVoltAvg, ui_labelBatteryVoltLow};
  const uint16_t cell[] = {data->highest_cell_voltage_100uv,
                           data->average_cell_voltage_100uv,
                           data->lowest_cell_voltage_100uv};
  const unsigned cell_signals[] = {8, 9, 7};
  bool voltage_valid = fresh[0] || fresh[1];
  uint16_t voltage =
      fresh[0] ? data->pack_voltage_deci_volts : data->pack_summed_voltage_deci_volts;
  bool power_valid = fresh[2] && voltage_valid && data->power_valid;
  lv_obj_t *powers[] = {ui_labelBatteryPowerHigh, ui_labelBatteryPowerAvg, ui_labelBatteryPowerLow};
  const int16_t power[] = {
      data->highest_pack_power_kw, data->average_pack_power_kw, data->lowest_pack_power_kw};
  lv_obj_t *currents[] = {ui_labelDebugSpeedHigh, ui_labelDebugSpeedAvg, ui_labelDebugSpeedLow};
  const int16_t current[] = {data->highest_pack_current_deci_amps,
                             data->average_pack_current_deci_amps,
                             data->lowest_pack_current_deci_amps};
  for (unsigned i = 0; i < 3; ++i) {
    if (fresh[temp_signals[i]])
      app_ui_set_signed_integer_label(temperatures[i], temperature[i]);
    else
      lv_label_set_text(temperatures[i], "--");
    if (fresh[cell_signals[i]])
      app_ui_set_voltage_mv_label(cells[i], cell[i]);
    else
      lv_label_set_text(cells[i], "--");
    if (power_valid)
      app_ui_set_signed_integer_label(powers[i], power[i]);
    else
      lv_label_set_text(powers[i], "--");
    if (fresh[2])
      ui_current(currents[i], current[i]);
    else
      lv_label_set_text(currents[i], "--");
  }
  ui_set_batt_temp_slider_value(app_ui_slider_from_temperature(data->highest_temperature_c));
  int64_t power_numerator = (int64_t)data->pack_current_deci_amps * voltage;
  int32_t watts_kw =
      (int32_t)((power_numerator >= 0 ? power_numerator + 50000 : power_numerator - 50000) /
                100000);
  ui_set_power_consumption_slider_value(app_ui_abs_int32(watts_kw));
  ui_set_batt_level_slider_value(data->pack_state_of_charge);
  ui_gauge_valid(ui_imgBattTempFill, ui_imgBattTempKnob, fresh[4]);
  ui_gauge_valid(ui_imgPowerConsumptionFill, ui_imgPowerConsumptionKnob, power_valid);
  ui_gauge_valid(ui_imgBattLevelFill, ui_imgBattLevelKnob, fresh[3]);
  lv_obj_set_style_text_font(ui_labelBatteryTemperature, &ui_font_TomorrowBoldItalic14, 0);
  lv_obj_set_style_text_font(ui_labelPowerConsumption, &ui_font_TomorrowBoldItalic14, 0);
  lv_obj_set_style_text_font(ui_labelBatteryLevel, &ui_font_TomorrowBoldItalic14, 0);
  lv_label_set_text_fmt(
      ui_labelBatteryTemperature, fresh[4] ? "TEMP %dC" : "TEMP --", data->highest_temperature_c);
  lv_label_set_text_fmt(
      ui_labelPowerConsumption, power_valid ? "PWR %ldkW" : "PWR --", (long)watts_kw);
  lv_label_set_text_fmt(
      ui_labelBatteryLevel, fresh[3] ? "BATT %u%%" : "BATT --", data->pack_state_of_charge);
  unsigned detail_signal[] = {0, 1, 3};
  for (unsigned i = 0; i < 3; ++i) {
    unsigned signal = detail_signal[i];
    if (!fresh[signal])
      lv_label_set_text(ui_DetailValues[i], "--");
    else if (i == 2)
      lv_label_set_text_fmt(ui_DetailValues[i], "%u", data->pack_state_of_charge);
    else {
      uint16_t v = i == 0 ? data->pack_voltage_deci_volts : data->pack_summed_voltage_deci_volts;
      lv_label_set_text_fmt(ui_DetailValues[i], "%u.%u", v / 10U, v % 10U);
    }
    lv_label_set_text(ui_DetailStates[i],
                      fresh[signal]                         ? "LIVE"
                      : (data->valid_mask & (1U << signal)) ? "STALE"
                                                            : "MISSING");
  }
  app_emmc_log_status_t storage = {0};
  app_emmc_log_get_status(&storage);
  uint32_t ram =
      app_ui_calculate_usage_percent(app_data_log_get_count(), app_data_log_get_capacity());
  uint32_t emmc =
      storage.reserved_blocks > 1U
          ? app_ui_calculate_usage_percent(storage.written_blocks * 512U + storage.staged_bytes,
                                           (storage.reserved_blocks - 1U) * 512U)
          : 0U;
  lv_label_set_text_fmt(
      ui_DetailValues[3], "RAM %lu%% MMC %lu%%", (unsigned long)ram, (unsigned long)emmc);
  lv_obj_set_style_text_font(ui_DetailValues[3], &ui_font_TomorrowBoldItalic12, 0);
  lv_label_set_text(ui_DetailStates[3],
                    !storage.ready || storage.io_failed ? "ERROR"
                    : storage.overflow                  ? "FULL"
                                                        : "READY");
}
/** Compatibility binding uses LVGL time; production paths use explicit shared clock below. */
void app_ui_apply_dashboard_data(const app_can_dashboard_data_t *data) {
  app_ui_apply_at(data, lv_tick_get());
}
/** Render live state and append diagnostic transitions, never clearing previous events. */
void app_ui_refresh(const app_can_dashboard_data_t *data,
                    const app_aux_ui_state_t *aux,
                    uint32_t now_ms) {
  if (data == NULL || aux == NULL)
    return;
  app_ui_apply_at(data, now_ms);
  app_ui_apply_aux_state(aux, now_ms);
  bool all_fresh = true;
  for (unsigned i = 0; i < APP_CAN_SIGNAL_COUNT; ++i) {
    bool fresh = ui_fresh(data, i, now_ms);
    all_fresh = all_fresh && fresh;
    if (now_ms >= APP_CAN_STALE_MS || fresh)
      app_fault_history_observe((uint16_t)i,
                                fresh ? APP_FAULT_STATE_HEALTHY : APP_FAULT_STATE_UNKNOWN,
                                fresh                            ? 0U
                                : (data->valid_mask & (1U << i)) ? 1U
                                                                 : 0U,
                                now_ms);
  }
  for (unsigned i = 0; i < APP_STATUS_SIGNAL_COUNT; ++i) {
    const app_status_signal_state_t *status = &aux->status_signals[i];
    if (status->valid || now_ms >= APP_CAN_STALE_MS)
      app_fault_history_observe((uint16_t)(10U + i),
                                !status->valid         ? APP_FAULT_STATE_UNKNOWN
                                : status->fault_active ? APP_FAULT_STATE_ACTIVE
                                                       : APP_FAULT_STATE_HEALTHY,
                                0,
                                now_ms);
  }
  if (now_ms >= APP_CAN_STALE_MS) {
    app_fault_history_observe(APP_FAULT_SPEED,
                              aux->speed_valid ? APP_FAULT_STATE_HEALTHY : APP_FAULT_STATE_UNKNOWN,
                              0,
                              now_ms);
    app_fault_history_observe(APP_FAULT_MODE,
                              aux->drive_mode_valid && aux->drive_mode < APP_DRIVE_MODE_COUNT
                                  ? APP_FAULT_STATE_HEALTHY
                                  : APP_FAULT_STATE_UNKNOWN,
                              0,
                              now_ms);
  }
  if (data->rejected_frames != 0U)
    app_fault_history_observe(
        APP_FAULT_CAN_REJECTED, APP_FAULT_STATE_ACTIVE, data->rejected_frames, now_ms);
  if (data->request_timeouts != 0U)
    app_fault_history_observe(APP_FAULT_CAN_TIMEOUT,
                              all_fresh ? APP_FAULT_STATE_HEALTHY : APP_FAULT_STATE_ACTIVE,
                              data->request_timeouts,
                              now_ms);
  app_emmc_log_status_t storage = {0};
  app_emmc_log_get_status(&storage);
  static uint32_t last_io_errors;
  if (storage.io_errors != last_io_errors) {
    app_fault_history_observe(APP_FAULT_STORAGE, APP_FAULT_STATE_ACTIVE, storage.io_errors, now_ms);
    last_io_errors = storage.io_errors;
  }
  app_fault_history_observe(APP_FAULT_STORAGE,
                            storage.ready && !storage.io_failed ? APP_FAULT_STATE_HEALTHY
                                                                : APP_FAULT_STATE_ACTIVE,
                            storage.io_errors,
                            now_ms);
  app_fault_history_observe(APP_FAULT_STORAGE_FULL,
                            storage.overflow ? APP_FAULT_STATE_ACTIVE : APP_FAULT_STATE_HEALTHY,
                            0,
                            now_ms);
  app_fault_history_flush();
  app_fault_history_observe(21U,
                            app_fault_history_durable() ? APP_FAULT_STATE_HEALTHY
                                                        : APP_FAULT_STATE_ACTIVE,
                            0,
                            now_ms);
  ui_fault_history_refresh();
}
