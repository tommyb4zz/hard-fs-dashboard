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
#include "app_emmc_log.h"
#include "render.h"
#include "ui/ui.h"

static uint32_t test_ram_count = 0U;
static uint32_t test_ram_capacity = 4096U;
static app_emmc_log_status_t test_emmc_status = {0};

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
static void assert_text(lv_obj_t *label, const char *expected) {
  assert(label != NULL);
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
  assert(state.drive_mode_valid);
  assert(state.drive_mode == APP_DRIVE_MODE_NEUTRAL);
  assert(!state.status_inputs_initialized);
  for (uint32_t i = 0U; i < APP_STATUS_SIGNAL_COUNT; ++i) {
    assert(state.status_signals[i].valid);
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
                                   .pack_state_of_charge = 255U};
  test_ram_count = 2048U;
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
  assert_text(ui_labelDebugSpeedHigh, "50%");
  assert_text(ui_labelDebugSpeedAvg, "50%");
  test_emmc_status.written_blocks = 262143U;
  app_ui_apply_dashboard_data(&data);
  assert_text(ui_labelDebugSpeedAvg, "100%");
  test_ram_capacity = 0U;
  test_emmc_status.reserved_blocks = 1U;
  app_ui_apply_dashboard_data(&data);
  assert_text(ui_labelDebugSpeedHigh, "0%");
  assert_text(ui_labelDebugSpeedAvg, "0%");
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
  assert(argc == 2);
  assert(dashboard_render_init());
  assert(dashboard_render_init());
  test_auxiliary_unknowns_and_faults();
  test_existing_demo();
  test_battery_labels_and_storage_usage();
  dashboard_render_step(10000U);
  test_framebuffer_capture(argv[1]);
  dashboard_render_shutdown();
  dashboard_render_shutdown();
  assert(!dashboard_render_capture(argv[1]));
  puts("dashboard UI unknown, demo, battery, usage and framebuffer checks passed");
  return 0;
}
