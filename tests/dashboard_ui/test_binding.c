/**
 * File purpose: Verify the production LVGL dashboard binding with known, unknown and
 * faulted synthetic states, signed battery values, storage percentage boundaries and
 * real memory-framebuffer captures. Storage counters are supplied by local test stubs.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_dashboard_ui.h"
#include "app_data_log.h"
#include "app_diagnostics_ui.h"
#include "app_emmc_log.h"
#include "app_fault_history.h"
#include "app_fault_monitor.h"
#include "render.h"
#include "ui/ui.h"

static uint32_t test_ram_count = 0U;
static uint32_t test_ram_capacity = 4096U;
static app_emmc_log_status_t test_emmc_status = {0};

/* Always-working RAM journal, attached like the firmware attaches the eMMC journal. */
#define TEST_JOURNAL_SLOTS 4096U
static app_fault_event_t test_journal[TEST_JOURNAL_SLOTS];
static bool test_journal_used[TEST_JOURNAL_SLOTS];

/** Return a stored slot, or erased for slots never written. */
static int test_journal_read(uint32_t index, app_fault_event_t *event) {
  if (index >= TEST_JOURNAL_SLOTS)
    return APP_FAULT_SLOT_IO_ERROR;
  if (!test_journal_used[index])
    return APP_FAULT_SLOT_ERASED;
  *event = test_journal[index];
  return APP_FAULT_SLOT_VALID;
}

/** Write an erased slot once; occupied slots are refused like the eMMC writer. */
static bool test_journal_write(uint32_t index, const app_fault_event_t *event) {
  if (index >= TEST_JOURNAL_SLOTS || test_journal_used[index])
    return false;
  test_journal[index] = *event;
  test_journal_used[index] = true;
  return true;
}

/**
 * Return the test-controlled RAM count used by the production debug-screen binding.
 *
 * Inputs: none. Returns test_ram_count without mutation so each test can choose exact
 * usage values independently of the real RAM logger.
 */
uint32_t app_data_log_get_count(void) {
  return test_ram_count;
}

/**
 * Return the test-controlled RAM capacity, including the zero-capacity boundary.
 *
 * Inputs: none. Returns test_ram_capacity without mutation; zero intentionally exercises
 * the UI division guard.
 */
uint32_t app_data_log_get_capacity(void) {
  return test_ram_capacity;
}

/**
 * Copy the test-controlled eMMC usage counters without performing any device operation.
 *
 * Input: status must be writable. Returns nothing; copies the configured test structure
 * including reserved/written block counters. Unlike production this stub uses no mutex or
 * storage calls.
 */
void app_emmc_log_get_status(app_emmc_log_status_t *status) {
  *status = test_emmc_status;
}

/**
 * Assert that an existing LVGL label contains exactly the required display string.
 *
 * Inputs: label must exist and expected is the exact terminated output string. Returns
 * nothing; assert aborts the process if the handle/text is wrong. It reads rendered UI
 * state without modifying it.
 */
#define assert_text(label, expected) assert_text_at((label), (expected), __LINE__)
/** Abort unless label shows exactly expected; prints both texts and the caller's line first. */
static void assert_text_at(lv_obj_t *label, const char *expected, int line) {
  assert(label != NULL);
  if (strcmp(lv_label_get_text(label), expected) != 0)
    fprintf(stderr,
            "line %d: label text '%s', expected '%s'\n",
            line,
            lv_label_get_text(label),
            expected);
  assert(strcmp(lv_label_get_text(label), expected) == 0);
}

/**
 * Compare a panel's effective RGB565 border color to LVGL's conversion of a hex color.
 *
 * Inputs: panel is an existing object and expected is a 24-bit RGB hex color. Returns
 * nothing; compares both sides after LVGL RGB565 conversion so precision loss is accounted
 * for. Assertion failure aborts execution.
 */
static void assert_border_color(lv_obj_t *panel, uint32_t expected) {
  assert(lv_color_to32(lv_obj_get_style_border_color(panel, LV_PART_MAIN)) ==
         lv_color_to32(lv_color_hex(expected)));
}

/**
 * Verify explicit unknowns stay visible and replace values left by an earlier sample.
 *
 * Inputs: none; renderer-created objects must exist. Returns nothing or aborts on
 * mismatch. Writes unknown, all-healthy, each single fault and invalid-mode states at tick
 * 4000. It proves group-wide visibility: all healthy hides all seven panels, any
 * unknown/fault shows all, each border reflects its own validity/fault.
 */
static void test_auxiliary_unknowns_and_faults(void) {
  app_aux_ui_state_t state = {.status_inputs_initialized = true};
  lv_obj_t *panels[] = {ui_panelIMD,
                        ui_panelFault,
                        ui_panelRTD,
                        ui_panelTSAL,
                        ui_panelBSPD,
                        ui_panelSDC,
                        ui_panleReadyToDrive};
  app_ui_apply_aux_state(&state, 4000U);
  assert_text(ui_labelSpeed, "-");
  assert_text(ui_labelDriveMode, "-");
  assert_border_color(ui_panelDriveMode, 0x808080U);
  for (uint32_t i = 0U; i < APP_STATUS_SIGNAL_COUNT; ++i) {
    assert(!lv_obj_has_flag(panels[i], LV_OBJ_FLAG_HIDDEN));
    assert_border_color(panels[i], 0x808080U);
    state.status_signals[i].valid = true;
  }
  state.speed_valid = true;
  state.speed_kph = 123U;
  state.drive_mode_valid = true;
  state.drive_mode = APP_DRIVE_MODE_DRIVE;
  app_ui_apply_aux_state(&state, 4000U);
  assert_text(ui_labelSpeed, "123");
  assert_text(ui_labelDriveMode, "D");
  for (uint32_t i = 0U; i < APP_STATUS_SIGNAL_COUNT; ++i) {
    assert(lv_obj_has_flag(panels[i], LV_OBJ_FLAG_HIDDEN));
    assert_border_color(panels[i], 0x52BA00U);
  }
  for (uint32_t fault = 0U; fault < APP_STATUS_SIGNAL_COUNT; ++fault) {
    for (uint32_t i = 0U; i < APP_STATUS_SIGNAL_COUNT; ++i) {
      state.status_signals[i].fault_active = (i == fault);
    }
    app_ui_apply_aux_state(&state, 4000U);
    for (uint32_t i = 0U; i < APP_STATUS_SIGNAL_COUNT; ++i) {
      assert(!lv_obj_has_flag(panels[i], LV_OBJ_FLAG_HIDDEN));
      assert_border_color(panels[i], i == fault ? 0xF40D0DU : 0x52BA00U);
    }
  }
  state.status_signals[APP_STATUS_SIGNAL_READY_TO_DRIVE].fault_active = false;
  state.status_signals[APP_STATUS_SIGNAL_BSPD].fault_active = true;
  state.status_signals[APP_STATUS_SIGNAL_BSPD].valid = false;
  state.speed_valid = false;
  state.drive_mode_valid = false;
  app_ui_apply_aux_state(&state, 4000U);
  assert_text(ui_labelSpeed, "-");
  assert_text(ui_labelDriveMode, "-");
  assert_border_color(ui_panelBSPD, 0x808080U);
  state.drive_mode_valid = true;
  state.drive_mode = APP_DRIVE_MODE_COUNT;
  app_ui_apply_aux_state(&state, 4000U);
  assert_text(ui_labelDriveMode, "-");
  assert_border_color(ui_panelDriveMode, 0x808080U);
}

/**
 * Verify demo speed/mode and known-healthy statuses retain their original timing rules.
 *
 * Inputs: none. Returns nothing or aborts; updates one local placeholder state at
 * 0,200,3000,5000,10000 ms. This verifies retained firmware demonstration timing rather
 * than deriving modes/speed from vehicle CAN.
 */
static void test_existing_demo(void) {
  app_aux_ui_state_t state = {0};
  app_ui_update_aux_placeholders(&state, 0U);
  assert(!state.speed_valid);
  assert(state.speed_wired && state.drive_mode_wired);
  assert(state.drive_mode_valid);
  assert(state.drive_mode == APP_DRIVE_MODE_NEUTRAL);
  assert(!state.status_inputs_initialized);
  for (uint32_t i = 0U; i < APP_STATUS_SIGNAL_COUNT; ++i) {
    assert(state.status_signals[i].valid);
    assert(state.status_signals[i].wired);
    assert(!state.status_signals[i].fault_active);
  }
  app_ui_update_aux_placeholders(&state, 200U);
  assert(state.speed_valid);
  assert(state.speed_kph == 3U);
  app_ui_update_aux_placeholders(&state, 3000U);
  assert(state.status_inputs_initialized);
  app_ui_update_aux_placeholders(&state, 5000U);
  assert(state.drive_mode == APP_DRIVE_MODE_DRIVE);
  app_ui_update_aux_placeholders(&state, 10000U);
  assert(state.drive_mode == APP_DRIVE_MODE_REVERSE);
}

/**
 * Verify signed measurement labels, voltage rounding and usage without uint32 overflow.
 *
 * Inputs: none. Returns nothing or aborts; supplies signed temperature/power extremes,
 * rounded cell millivolts and large usage counters. Test-controlled globals are changed,
 * including zero capacity, to prove safe percentage formatting and the one-header-block
 * subtraction.
 */
static void test_battery_labels_and_storage_usage(void) {
  app_can_dashboard_data_t data = {.highest_temperature_c = 127,
                                   .average_temperature_c = -1,
                                   .lowest_temperature_c = -128,
                                   .lowest_cell_voltage_100uv = 34564U,
                                   .average_cell_voltage_100uv = 34565U,
                                   .highest_cell_voltage_100uv = 65535U,
                                   .lowest_pack_power_kw = -32768,
                                   .average_pack_power_kw = -4,
                                   .highest_pack_power_kw = 32767,
                                   .pack_current_deci_amps = -32768,
                                   .pack_state_of_charge = 255U,
                                   .highest_pack_current_deci_amps = 32767,
                                   .average_pack_current_deci_amps = -12,
                                   .lowest_pack_current_deci_amps = -32768,
                                   .valid_mask = (1U << APP_CAN_SIGNAL_COUNT) - 1U,
                                   .power_valid = true};
  test_ram_count = 2048U;
  test_emmc_status.ready = true;
  test_emmc_status.reserved_blocks = 262144U;
  test_emmc_status.written_blocks = 131071U;
  app_ui_apply_dashboard_data(&data);
  assert_text(ui_labelBatteryTempHigh, "127");
  assert_text(ui_labelBatteryTempAvg, "-1");
  assert_text(ui_labelBatteryTempLow, "-128");
  assert_text(ui_labelBatteryVoltLow, "3456");
  assert_text(ui_labelBatteryVoltAvg, "3457");
  assert_text(ui_labelBatteryVoltHigh, "6554");
  assert_text(ui_labelBatteryPowerLow, "-32768");
  assert_text(ui_labelBatteryPowerAvg, "-4");
  assert_text(ui_labelBatteryPowerHigh, "32767");
  /* The SquareLine SPEED row is speed only; signed pack current is on the pack screen. */
  assert_text(app_ui_pack_values[APP_PACK_ROW_CURRENT], "-3276.8");
  assert_text(app_ui_pack_states[APP_PACK_ROW_CURRENT], "LIVE");
  assert_text(app_ui_pack_values[APP_PACK_ROW_SOC], "255");
  assert_text(app_ui_history_status, "CAN timeouts 0 | bad frames 0 | RAM 50% | MMC 50% READY");
  test_emmc_status.written_blocks = 262143U;
  data.request_timeouts = 12U;
  data.rejected_frames = 3U;
  app_ui_apply_dashboard_data(&data);
  assert_text(app_ui_history_status, "CAN timeouts 12 | bad frames 3 | RAM 50% | MMC 100% READY");
  test_ram_capacity = 0U;
  test_emmc_status.reserved_blocks = 1U;
  app_ui_apply_dashboard_data(&data);
  assert_text(app_ui_history_status, "CAN timeouts 12 | bad frames 3 | RAM 0% | MMC 0% READY");
  test_emmc_status.ready = false;
  app_ui_apply_dashboard_data(&data);
  assert_text(app_ui_history_status, "CAN timeouts 12 | bad frames 3 | RAM 0% | MMC 0% ERROR");
  test_ram_capacity = 4096U;
  test_emmc_status.ready = true;
}

/*
 * The firmware refreshes once before the first frame, so no SquareLine placeholder text
 * ("120", "N", "0", "BSPB") or measured zero is ever shown, and gauges start hidden.
 */
static void test_initial_measurements_are_unknown(void) {
  app_can_dashboard_data_t data = {0};
  app_aux_ui_state_t aux = {0};
  test_emmc_status.ready = true;
  app_ui_refresh(&data, &aux, 0U);
  lv_obj_t *labels[] = {ui_labelBatteryVoltHigh,
                        ui_labelBatteryVoltAvg,
                        ui_labelBatteryVoltLow,
                        ui_labelBatteryTempHigh,
                        ui_labelBatteryTempAvg,
                        ui_labelBatteryTempLow,
                        ui_labelBatteryPowerHigh,
                        ui_labelBatteryPowerAvg,
                        ui_labelBatteryPowerLow,
                        ui_labelDebugSpeedHigh,
                        ui_labelDebugSpeedAvg,
                        ui_labelDebugSpeedLow};
  for (unsigned i = 0U; i < sizeof(labels) / sizeof(labels[0]); i++)
    assert_text(labels[i], "--");
  for (unsigned i = 0U; i < APP_PACK_ROW_COUNT; i++) {
    assert_text(app_ui_pack_values[i], "--");
    assert_text(app_ui_pack_states[i], "MISSING");
  }
  assert_text(ui_labelSpeed, "-");
  assert_text(ui_labelDriveMode, "-");
  assert_text(ui_labelBSPD, "BSPD");
  /* The cell table is the SquareLine DebugFrame image, not a redrawn substitute. */
  assert(!lv_obj_has_flag(ui_Image1, LV_OBJ_FLAG_HIDDEN));
  assert(lv_img_get_src(ui_Image1) == &ui_img_debugframe_png);
  assert(app_fault_history_count() == 0U);
  assert_text(app_ui_history_rows[0], "No malfunctions recorded");
  assert_text(app_ui_history_journal, "SAVED TO CARD");
  assert_text(ui_labelBatteryTemperature, "TEMP --");
  assert_text(ui_labelPowerConsumption, "PWR --");
  assert_text(ui_labelBatteryLevel, "BATT --");
  lv_obj_t *hidden[] = {ui_imgBattTempFill,
                        ui_imgBattTempKnob,
                        ui_imgPowerConsumptionFill,
                        ui_imgPowerConsumptionKnob,
                        ui_imgBattLevelFill,
                        ui_imgBattLevelKnob};
  for (unsigned i = 0U; i < sizeof(hidden) / sizeof(hidden[0]); i++)
    assert(lv_obj_has_flag(hidden[i], LV_OBJ_FLAG_HIDDEN));
  assert(!lv_obj_has_flag(ui_imgBattTempIco, LV_OBJ_FLAG_HIDDEN));
  assert(!lv_obj_has_flag(ui_imgPowerConsumptionIco, LV_OBJ_FLAG_HIDDEN));
  assert(!lv_obj_has_flag(ui_imgBattLevelIco, LV_OBJ_FLAG_HIDDEN));
}

/** Return a fully received battery snapshot with every signal stamped at tick. */
static app_can_dashboard_data_t known_battery(uint32_t tick) {
  app_can_dashboard_data_t data = {.pack_voltage_deci_volts = 4000U,
                                   .pack_summed_voltage_deci_volts = 3998U,
                                   .pack_current_deci_amps = -1234,
                                   .pack_state_of_charge = 81U,
                                   .highest_temperature_c = 72,
                                   .average_temperature_c = 35,
                                   .lowest_temperature_c = 20,
                                   .highest_cell_voltage_100uv = 41000U,
                                   .average_cell_voltage_100uv = 37000U,
                                   .lowest_cell_voltage_100uv = 32000U,
                                   .highest_pack_power_kw = 80,
                                   .average_pack_power_kw = 13,
                                   .lowest_pack_power_kw = -40,
                                   .highest_pack_current_deci_amps = 2000,
                                   .average_pack_current_deci_amps = 333,
                                   .lowest_pack_current_deci_amps = -1000,
                                   .valid_mask = (1U << APP_CAN_SIGNAL_COUNT) - 1U,
                                   .power_valid = true};
  for (unsigned i = 0U; i < APP_CAN_SIGNAL_COUNT; i++)
    data.signal_tick_ms[i] = tick;
  return data;
}

/** Return wired, valid and healthy auxiliary inputs at 42 km/h in drive. */
static app_aux_ui_state_t healthy_auxiliary(void) {
  app_aux_ui_state_t aux = {.speed_kph = 42U,
                            .speed_valid = true,
                            .speed_wired = true,
                            .drive_mode = APP_DRIVE_MODE_DRIVE,
                            .drive_mode_valid = true,
                            .drive_mode_wired = true,
                            .status_inputs_initialized = true};
  for (unsigned i = 0U; i < APP_STATUS_SIGNAL_COUNT; i++) {
    aux.status_signals[i].valid = true;
    aux.status_signals[i].wired = true;
  }
  return aux;
}

/** Return the first event at or after index from source, or UINT32_MAX when none exists. */
static uint32_t find_event(uint32_t index, uint16_t source, app_fault_event_t *event) {
  for (; index < app_fault_history_count(); index++)
    if (app_fault_history_get(index, event) && event->source == source)
      return index;
  return UINT32_MAX;
}

/* Per-signal age controls labels and gauges independently, including genuine zero values. */
static void test_freshness_and_missing_siblings(void) {
  app_can_dashboard_data_t data = known_battery(1000U);
  app_aux_ui_state_t aux = healthy_auxiliary();
  test_emmc_status.ready = true;
  app_ui_refresh(&data, &aux, 1000U + APP_CAN_STALE_MS);
  assert_text(ui_labelBatteryTempHigh, "72");
  assert_text(ui_labelBatteryTempAvg, "35");
  assert_text(ui_labelBatteryTempLow, "20");
  assert_text(app_ui_pack_values[APP_PACK_ROW_VOLTAGE], "400.0");
  assert_text(app_ui_pack_states[APP_PACK_ROW_VOLTAGE], "LIVE");
  assert_text(app_ui_pack_values[APP_PACK_ROW_SUMMED], "399.8");
  assert_text(app_ui_pack_values[APP_PACK_ROW_CURRENT], "-123.4");
  assert(!lv_obj_has_flag(ui_imgBattTempFill, LV_OBJ_FLAG_HIDDEN));
  assert(!lv_obj_has_flag(ui_imgPowerConsumptionFill, LV_OBJ_FLAG_HIDDEN));

  data.signal_tick_ms[APP_CAN_SIGNAL_SOC] = 3001U;
  app_ui_refresh(&data, &aux, 3001U);
  assert_text(ui_labelBatteryTempHigh, "--");
  assert_text(ui_labelBatteryTempAvg, "--");
  assert_text(ui_labelBatteryTempLow, "--");
  assert_text(ui_labelBatteryPowerHigh, "--");
  assert_text(app_ui_pack_values[APP_PACK_ROW_VOLTAGE], "--");
  assert_text(app_ui_pack_states[APP_PACK_ROW_VOLTAGE], "STALE");
  assert_text(app_ui_pack_values[APP_PACK_ROW_SOC], "81");
  assert_text(app_ui_pack_states[APP_PACK_ROW_SOC], "LIVE");
  assert(lv_obj_has_flag(ui_imgBattTempFill, LV_OBJ_FLAG_HIDDEN));
  assert(lv_obj_has_flag(ui_imgPowerConsumptionFill, LV_OBJ_FLAG_HIDDEN));
  assert(!lv_obj_has_flag(ui_imgBattLevelFill, LV_OBJ_FLAG_HIDDEN));

  data.highest_temperature_c = 0;
  data.signal_tick_ms[APP_CAN_SIGNAL_TEMP_HIGH] = 3001U;
  app_ui_refresh(&data, &aux, 3001U);
  assert_text(ui_labelBatteryTempHigh, "0");
  assert_text(ui_labelBatteryTempAvg, "--");
  assert_text(ui_labelBatteryTemperature, "TEMP 0C");
  assert(!lv_obj_has_flag(ui_imgBattTempFill, LV_OBJ_FLAG_HIDDEN));

  data = known_battery(4000U);
  data.valid_mask &= (uint16_t) ~(1U << APP_CAN_SIGNAL_TEMP_AVG);
  app_ui_refresh(&data, &aux, 4000U);
  assert_text(ui_labelBatteryTempHigh, "72");
  assert_text(ui_labelBatteryTempAvg, "--");
  assert_text(ui_labelBatteryTempLow, "20");
  assert_text(ui_labelBatteryVoltAvg, "3700");
}

/*
 * Clearing a live fault appends recovery once the hold time has passed and leaves the
 * raised event intact. Other sources may record their own episodes meanwhile, so the IMD
 * entries are located by source.
 */
static void test_fault_events_survive_recovery(void) {
  app_can_dashboard_data_t data = known_battery(6000U);
  app_aux_ui_state_t aux = healthy_auxiliary();
  const uint16_t imd = APP_FAULT_AUX_STATUS_FIRST + APP_STATUS_SIGNAL_IMD;
  test_emmc_status.ready = true;
  test_emmc_status.overflow = false;
  app_ui_refresh(&data, &aux, 6000U);
  uint32_t before = app_fault_history_count();
  aux.status_signals[APP_STATUS_SIGNAL_IMD].fault_active = true;
  app_ui_refresh(&data, &aux, 6001U);
  app_fault_event_t raised = {0}, event = {0};
  uint32_t raised_index = find_event(before, imd, &raised);
  assert(raised_index != UINT32_MAX);
  assert(raised.state == APP_FAULT_STATE_ACTIVE && raised.timestamp_ms == 6001U);
  app_ui_refresh(&data, &aux, 6001U);
  assert(find_event(raised_index + 1U, imd, &event) == UINT32_MAX);
  aux.status_signals[APP_STATUS_SIGNAL_IMD].fault_active = false;
  data = known_battery(6002U);
  app_ui_refresh(&data, &aux, 6002U);
  assert(find_event(raised_index + 1U, imd, &event) == UINT32_MAX); /* Hold not elapsed. */
  data = known_battery(6002U + APP_FAULT_CLEAR_HOLD_MS);
  app_ui_refresh(&data, &aux, 6002U + APP_FAULT_CLEAR_HOLD_MS);
  assert(find_event(raised_index + 1U, imd, &event) != UINT32_MAX);
  assert(event.state == APP_FAULT_STATE_HEALTHY);
  assert(event.timestamp_ms == 6002U + APP_FAULT_CLEAR_HOLD_MS);
  assert(app_fault_history_get(raised_index, &event));
  assert(event.sequence == raised.sequence && event.session_id == raised.session_id);
  assert(event.timestamp_ms == raised.timestamp_ms && event.source == raised.source);
  assert(event.state == raised.state && event.detail == raised.detail);
  event.detail = UINT32_MAX;
  assert(app_fault_history_get(raised_index, &event));
  assert(event.detail == raised.detail);
}

/* A write error recovered between GUI refreshes is still one FAULT and, later, one CLEAR. */
static void test_transient_storage_failure_is_retained(void) {
  uint32_t now = 6002U + APP_FAULT_CLEAR_HOLD_MS + 100U;
  app_can_dashboard_data_t data = known_battery(now);
  app_aux_ui_state_t aux = healthy_auxiliary();
  test_emmc_status.ready = true;
  test_emmc_status.io_failed = false;
  app_ui_refresh(&data, &aux, now);
  uint32_t before = app_fault_history_count();
  test_emmc_status.io_errors++;
  app_ui_refresh(&data, &aux, now + 1U);
  app_fault_event_t raised = {0}, cleared = {0};
  uint32_t index = find_event(before, APP_FAULT_STORAGE, &raised);
  assert(index != UINT32_MAX && raised.state == APP_FAULT_STATE_ACTIVE);
  assert(raised.detail == APP_FAULT_STORAGE_WRITE_ERRORS && raised.timestamp_ms == now + 1U);
  app_ui_refresh(&data, &aux, now + 2U);
  assert(find_event(index + 1U, APP_FAULT_STORAGE, &cleared) == UINT32_MAX);
  data = known_battery(now + 1U + APP_FAULT_ERROR_QUIET_MS);
  app_ui_refresh(&data, &aux, now + 1U + APP_FAULT_ERROR_QUIET_MS);
  assert(find_event(index + 1U, APP_FAULT_STORAGE, &cleared) != UINT32_MAX);
  assert(cleared.state == APP_FAULT_STATE_HEALTHY && cleared.detail == 1U);
}

/** Depth-first search for the clickable parent of a label whose text is caption. */
static lv_obj_t *find_button(lv_obj_t *root, const char *caption) {
  if (lv_obj_check_type(root, &lv_label_class) && strcmp(lv_label_get_text(root), caption) == 0) {
    lv_obj_t *parent = lv_obj_get_parent(root);
    if (parent != NULL && lv_obj_has_flag(parent, LV_OBJ_FLAG_CLICKABLE))
      return parent;
  }
  for (uint32_t i = 0U; i < lv_obj_get_child_cnt(root); i++) {
    lv_obj_t *button = find_button(lv_obj_get_child(root, (int32_t)i), caption);
    if (button != NULL)
      return button;
  }
  return NULL;
}

/** Send CLICKED to the button captioned caption on screen; abort if it does not exist. */
static void click_button(lv_obj_t *screen, const char *caption) {
  lv_obj_t *button = find_button(screen, caption);
  assert(button != NULL);
  assert(lv_event_send(button, LV_EVENT_CLICKED, NULL) == LV_RES_OK);
}

/** Capture the active screen to <base>.<name>.ppm for later inspection. */
static void capture_screen(const char *base, const char *name) {
  char path[4096];
  int length = snprintf(path, sizeof(path), "%s.%s.ppm", base, name);
  assert(length > 0 && (size_t)length < sizeof(path));
  assert(dashboard_render_capture(path));
}

/* Exercise the actual touch callbacks, including chronological history pagination. */
static void test_navigation_and_history_pages(const char *capture_path) {
  app_can_dashboard_data_t data = known_battery(7000U);
  app_aux_ui_state_t aux = healthy_auxiliary();
  test_ram_count = 2048U;
  test_ram_capacity = 4096U;
  test_emmc_status.reserved_blocks = 262144U;
  test_emmc_status.written_blocks = 131071U;
  app_ui_refresh(&data, &aux, 7000U);
  lv_disp_load_scr(ui_MainScreen);
  assert(lv_event_send(ui_MainScreen, LV_EVENT_LONG_PRESSED, NULL) == LV_RES_OK);
  dashboard_render_step(16000U);
  assert(lv_scr_act() == ui_DebugScreen);
  capture_screen(capture_path, "debug");
  click_button(ui_DebugScreen, "PACK");
  assert(lv_scr_act() == app_ui_pack_screen);
  assert(lv_obj_get_style_bg_opa(app_ui_pack_screen, LV_PART_MAIN) == LV_OPA_COVER);
  assert(lv_color_to32(lv_obj_get_style_bg_color(app_ui_pack_screen, LV_PART_MAIN)) ==
         lv_color_to32(lv_color_black()));
  capture_screen(capture_path, "pack");
  click_button(app_ui_pack_screen, "CELLS");
  assert(lv_scr_act() == ui_DebugScreen);
  click_button(ui_DebugScreen, "FAULTS");
  assert(lv_scr_act() == app_ui_history_screen);
  assert(lv_obj_get_style_bg_opa(app_ui_history_screen, LV_PART_MAIN) == LV_OPA_COVER);
  assert(lv_color_to32(lv_obj_get_style_bg_color(app_ui_history_screen, LV_PART_MAIN)) ==
         lv_color_to32(lv_color_black()));

  uint32_t count = app_fault_history_count();
  assert(count > 4U);
  char expected[32], latest[256];
  snprintf(expected, sizeof(expected), "#%lu ", (unsigned long)count);
  assert(strncmp(lv_label_get_text(app_ui_history_rows[0]), expected, strlen(expected)) == 0);
  snprintf(latest, sizeof(latest), "%s", lv_label_get_text(app_ui_history_rows[0]));
  click_button(app_ui_history_screen, "OLDER");
  snprintf(expected, sizeof(expected), "#%lu ", (unsigned long)(count - 4U));
  assert(strncmp(lv_label_get_text(app_ui_history_rows[0]), expected, strlen(expected)) == 0);
  click_button(app_ui_history_screen, "NEWER");
  assert_text(app_ui_history_rows[0], latest);
  click_button(app_ui_history_screen, "NEWER");
  assert_text(app_ui_history_rows[0], latest);

  uint32_t pages = (count + 3U) / 4U;
  for (uint32_t page = 0; page < pages + 1U; page++)
    click_button(app_ui_history_screen, "OLDER");
  snprintf(expected, sizeof(expected), "#%lu ", (unsigned long)(count - (pages - 1U) * 4U));
  assert(strncmp(lv_label_get_text(app_ui_history_rows[0]), expected, strlen(expected)) == 0);
  for (uint32_t page = 0; page < pages; page++)
    click_button(app_ui_history_screen, "NEWER");
  assert_text(app_ui_history_rows[0], latest);
  capture_screen(capture_path, "faults");
  click_button(app_ui_history_screen, "PACK");
  assert(lv_scr_act() == app_ui_pack_screen);
  /* Long press anywhere outside a button returns to main, like the SquareLine debug screen. */
  assert(lv_event_send(app_ui_pack_screen, LV_EVENT_LONG_PRESSED, NULL) == LV_RES_OK);
  dashboard_render_step(16500U);
  assert(lv_scr_act() == ui_MainScreen);
  lv_disp_load_scr(app_ui_history_screen);
  assert(lv_event_send(app_ui_history_screen, LV_EVENT_LONG_PRESSED, NULL) == LV_RES_OK);
  dashboard_render_step(17000U);
  assert(lv_scr_act() == ui_MainScreen);
  lv_disp_load_scr(app_ui_pack_screen);
  click_button(app_ui_pack_screen, "FAULTS");
  click_button(app_ui_history_screen, "MAIN");
  assert(lv_scr_act() == ui_MainScreen);
}

/** Press and release the simulated touch panel, giving LVGL time to read both states. */
static uint32_t touch_tap(int32_t x, int32_t y, uint32_t now, uint32_t hold_ms) {
  dashboard_render_touch(x, y, true);
  for (uint32_t elapsed = 0U; elapsed <= hold_ms; elapsed += 10U)
    dashboard_render_step(now += 10U);
  dashboard_render_touch(x, y, false);
  for (unsigned i = 0U; i < 12U; i++)
    dashboard_render_step(now += 10U);
  return now;
}

/* Real pointer input reaches the same callbacks as the board's touch panel. */
static void test_touch_navigation(void) {
  uint32_t now = 30000U;
  lv_disp_load_scr(ui_MainScreen);
  dashboard_render_step(now);
  now = touch_tap(240, 136, now, 600U); /* long press on the main screen */
  for (unsigned i = 0U; i < 20U; i++)
    dashboard_render_step(now += 10U);
  assert(lv_scr_act() == ui_DebugScreen);
  now = touch_tap(48, 31, now, 40U); /* PACK button in the table's empty header cell */
  assert(lv_scr_act() == app_ui_pack_screen);
  now = touch_tap(126, 31, now, 40U); /* FAULTS */
  assert(lv_scr_act() == app_ui_history_screen);
  now = touch_tap(204, 31, now, 40U); /* MAIN */
  assert(lv_scr_act() == ui_MainScreen);
  now = touch_tap(240, 136, now, 600U);
  for (unsigned i = 0U; i < 20U; i++)
    dashboard_render_step(now += 10U);
  now = touch_tap(48, 31, now, 40U);
  assert(lv_scr_act() == app_ui_pack_screen);
  now = touch_tap(300, 180, now, 600U); /* long press on the pack table returns to main */
  for (unsigned i = 0U; i < 20U; i++)
    dashboard_render_step(now += 10U);
  assert(lv_scr_act() == ui_MainScreen);
}

/* Binding tolerates missing runtime screens instead of reaching LVGL's NULL assertion. */
static void test_missing_runtime_screens_are_tolerated(void) {
  app_can_dashboard_data_t data = known_battery(40000U);
  app_aux_ui_state_t aux = healthy_auxiliary();
  app_diagnostics_ui_destroy();
  assert(app_ui_pack_screen == NULL && app_ui_history_rows[0] == NULL);
  app_ui_apply_dashboard_data(&data);
  app_diagnostics_ui_refresh_history();
  app_ui_refresh(&data, &aux, 40000U);
  assert(strcmp(app_diagnostics_ui_screen_name(), "main") == 0);
  assert_text(ui_labelBatteryTempHigh, "72");
}

/** Require white caption pixels where the pack screen's FAULTS button text is drawn. */
static void assert_faults_caption_has_pixels(const char *path) {
  FILE *image = fopen(path, "rb");
  assert(image != NULL);
  unsigned white_pixels = 0U;
  for (unsigned y = 24U; y < 40U; y++) {
    assert(fseek(image, 15L + ((long)y * 480L + 90L) * 3L, SEEK_SET) == 0);
    for (unsigned x = 90U; x < 162U; x++) {
      int red = fgetc(image), green = fgetc(image), blue = fgetc(image);
      assert(red != EOF && green != EOF && blue != EOF);
      if (red > 180 && green > 180 && blue > 180)
        white_pixels++;
    }
  }
  assert(fclose(image) == 0);
  assert(white_pixels > 20U);
}

/* A static page must retain identical pixels after repeated off-screen refreshes. */
static void test_repeated_captures_keep_navigation_text(const char *capture_path) {
  app_can_dashboard_data_t data = known_battery(0U);
  app_aux_ui_state_t aux = healthy_auxiliary();
  test_emmc_status.ready = true;
  char baseline_path[4096], repeated_path[4096];
  int length = snprintf(baseline_path, sizeof(baseline_path), "%s.nav-baseline.ppm", capture_path);
  assert(length > 0 && (size_t)length < sizeof(baseline_path));
  length = snprintf(repeated_path, sizeof(repeated_path), "%s.nav-repeated.ppm", capture_path);
  assert(length > 0 && (size_t)length < sizeof(repeated_path));
  app_ui_refresh(&data, &aux, 0U);
  for (unsigned cycle = 0U; cycle < 16U; cycle++) {
    lv_disp_load_scr(ui_MainScreen);
    data = known_battery(cycle * 1000U);
    app_ui_refresh(&data, &aux, cycle * 1000U);
    dashboard_render_step(cycle * 1000U);
    assert(dashboard_render_capture(repeated_path));
    lv_disp_load_scr(ui_DebugScreen);
    assert(dashboard_render_capture(repeated_path));
    lv_disp_load_scr(app_ui_pack_screen);
    assert(dashboard_render_capture(repeated_path));
    assert_faults_caption_has_pixels(repeated_path);
    if (cycle == 0U)
      assert(dashboard_render_capture(baseline_path));
    FILE *baseline = fopen(baseline_path, "rb");
    FILE *repeated = fopen(repeated_path, "rb");
    assert(baseline != NULL && repeated != NULL);
    for (;;) {
      int first = fgetc(baseline), next = fgetc(repeated);
      assert(first == next);
      if (first == EOF)
        break;
    }
    assert(!ferror(baseline) && !ferror(repeated));
    assert(fclose(baseline) == 0 && fclose(repeated) == 0);
    lv_disp_load_scr(app_ui_history_screen);
    assert(dashboard_render_capture(repeated_path));
  }
  lv_disp_load_scr(ui_MainScreen);
}

/**
 * Exercise real partial-buffer rendering, binary PPM size and file-error reporting.
 *
 * Input: output_path is the temporary PPM path chosen by CTest. Returns nothing or aborts;
 * writes/opens the actual framebuffer capture, checks header/byte length, closes it and
 * requires null/nonexistent-path failures. The file remains as test evidence.
 */
static void test_framebuffer_capture(const char *output_path) {
  assert(dashboard_render_capture(output_path));
  FILE *input = fopen(output_path, "rb");
  assert(input != NULL);
  char header[16] = {0};
  assert(fread(header, 1U, 15U, input) == 15U);
  assert(memcmp(header, "P6\n480 272\n255\n", 15U) == 0);
  assert(fseek(input, 0, SEEK_END) == 0);
  assert(ftell(input) == 15L + 480L * 272L * 3L);
  assert(fclose(input) == 0);
  assert(!dashboard_render_capture(NULL));
  assert(!dashboard_render_capture("/directory-that-does-not-exist/capture.ppm"));
}

/**
 * Run production UI regressions against a memory display and clean up its screen trees.
 *
 * Inputs: exactly one output path argument. Returns zero only after all assertion-based
 * UI/demo/storage/render checks complete. Initializes twice to test idempotence, advances
 * LVGL, captures, shuts down twice and requires capture rejection after teardown.
 * Assertions are kept enabled by the test target.
 */
int main(int argc, char **argv) {
  assert(argc == 2 || argc == 3);
  assert(dashboard_render_init());
  assert(dashboard_render_init());
  app_fault_history_init(1U);
  app_fault_history_attach(test_journal_read, test_journal_write, TEST_JOURNAL_SLOTS);
  if (argc == 3) {
    assert(strcmp(argv[2], "--capture-regression") == 0);
    test_repeated_captures_keep_navigation_text(argv[1]);
    dashboard_render_shutdown();
    puts("repeated screen-capture pixel regression passed");
    return 0;
  }
  test_initial_measurements_are_unknown();
  test_auxiliary_unknowns_and_faults();
  test_existing_demo();
  test_battery_labels_and_storage_usage();
  test_repeated_captures_keep_navigation_text(argv[1]);
  test_freshness_and_missing_siblings();
  test_fault_events_survive_recovery();
  test_transient_storage_failure_is_retained();
  test_navigation_and_history_pages(argv[1]);
  dashboard_render_step(25000U);
  test_touch_navigation();
  test_framebuffer_capture(argv[1]);
  test_missing_runtime_screens_are_tolerated();
  dashboard_render_shutdown();
  dashboard_render_shutdown();
  assert(!dashboard_render_capture(argv[1]));
  puts("dashboard UI freshness, fault retention, touch navigation, pagination and framebuffer "
       "checks passed");
  return 0;
}
