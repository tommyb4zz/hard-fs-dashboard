/* Purpose: step production CAN/UI/logging with memory-only HAL boundaries and an explicit clock.
 * stdin T commands supply auxiliary state plus frames; P captures the actual LVGL framebuffer;
 * E exports fake eMMC. This executable never opens a physical CAN or serial interface. */
#include "app_can.h"
#include "app_dashboard_ui.h"
#include "app_data_log.h"
#include "app_emmc_log.h"
#include "fake_hal.h"
#include "render.h"
#include "lvgl/lvgl.h"
#include "ui/screens/ui_MainScreen.h"
#include "ui/ui.h"
#include "ui/screens/ui_DebugScreen.h"
#include "app_fault_history.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Print a JSON string with control/quote escaping for real LVGL label values.
 *
 * Input: value is a valid terminated string. Returns nothing; writes quotes and escaped
 * content to stdout without allocating. ASCII control bytes use JSON Unicode escapes;
 * other bytes pass through, assuming generated label text is suitable for the JSON stream.
 */
static void json_string(const char *value) {
  putchar('"');
  for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
    if (*p == '"' || *p == '\\') {
      putchar('\\');
      putchar(*p);
    } else if (*p < 32U)
      printf("\\u%04x", *p);
    else
      putchar(*p);
  }
  putchar('"');
}
/**
 * Print an existing label's actual text, or JSON null when no object exists.
 *
 * Input: label is a label handle or NULL. Returns nothing; writes actual current LVGL text
 * rather than reconstructing a display value. The null branch writes a JSON literal and
 * avoids querying an absent object.
 */
static void json_label(lv_obj_t *label) {
  if (label == NULL)
    printf("null");
  else
    json_string(lv_label_get_text(label));
}
/**
 * Split exactly count whitespace-delimited tokens; reject missing or surplus fields.
 *
 * Inputs: mutable line, writable fields array and required token count. Returns true only
 * for exactly count tokens. strtok replaces delimiters with terminators and fields borrow
 * storage from line, so callers must parse them before reusing that buffer.
 */
static bool split_fields(char *line, char **fields, size_t count) {
  char *token = strtok(line, " \t\r\n");
  for (size_t i = 0; i < count; i++) {
    if (token == NULL)
      return false;
    fields[i] = token;
    token = strtok(NULL, " \t\r\n");
  }
  return token == NULL;
}
/**
 * Parse a complete bounded integer before narrowing; reject overflow and invalid syntax.
 *
 * Inputs: terminated text, base 10/16, inclusive int64-compatible bounds and writable
 * output. Returns true and writes output only for a fully parsed in-range value; output is
 * retained on failure. Initial digit checks, errno and end-pointer checks precede
 * narrowing conversions.
 */
static bool
parse_integer(const char *text, int base, long long minimum, long long maximum, long long *output) {
  const char *digits = text[0] == '-' ? text + 1 : text;
  if (base == 16 ? !isxdigit((unsigned char)*digits) : !isdigit((unsigned char)*digits))
    return false;
  errno = 0;
  char *end;
  long long value = strtoll(text, &end, base);
  if (errno == ERANGE || *end != '\0' || value < minimum || value > maximum)
    return false;
  *output = value;
  return true;
}
/**
 * Read one bounded frame, rejecting invalid syntax, metadata and noncanonical DLC sizes.
 *
 * Input: input is the stdin-like stream, which must supply one newline-terminated record:
 * hexadecimal id, three 0/1 metadata fields and payload hex or -. Returns true only after
 * a canonical DLC payload has been enqueued. The 64-byte scratch bounds hex decoding;
 * queue overflow, bad fields or truncated lines return false.
 */
static bool read_frame(FILE *input) {
  char line[256], *fields[5];
  long long id, extended, remote, fd;
  if (!fgets(line, sizeof(line), input) || !strchr(line, '\n') || !split_fields(line, fields, 5) ||
      !parse_integer(fields[0], 16, 0, 0x1FFFFFFF, &id) ||
      !parse_integer(fields[1], 10, 0, 1, &extended) ||
      !parse_integer(fields[2], 10, 0, 1, &remote) || !parse_integer(fields[3], 10, 0, 1, &fd))
    return false;
  const char *hex = fields[4];
  size_t length = strcmp(hex, "-") == 0 ? 0 : strlen(hex);
  if (length > 128 || length % 2)
    return false;
  size_t size = length / 2;
  uint8_t bytes[64] = {0};
  for (size_t i = 0; i < size; i++) {
    long long value;
    char pair[3] = {hex[2 * i], hex[2 * i + 1], 0};
    if (!isxdigit((unsigned char)hex[2 * i]) || !isxdigit((unsigned char)hex[2 * i + 1]) ||
        !parse_integer(pair, 16, 0, 255, &value))
      return false;
    bytes[i] = (uint8_t)value;
  }
  return dashboard_host_receive((uint32_t)id, extended != 0, remote != 0, fd != 0, bytes, size);
}
/**
 * Emit decoded production values, captured requests, actual label/status state and log
 * counters.
 *
 * Inputs: tick is simulated milliseconds, updated/logged describe this poll/append and
 * data is the retained production snapshot in its named integer units. Returns nothing;
 * consumes at most one pending production TX, queries real LVGL labels/styles and log
 * counters, emits one flushed JSON line. All panel handles must have been created before
 * calling.
 */
static void
print_step(uint32_t tick, bool updated, bool logged, const app_can_dashboard_data_t *data) {
  dashboard_host_tx_t tx;
  app_emmc_log_status_t status = {0};
  app_emmc_log_get_status(&status);
  printf("{\"tick_ms\":%lu,\"updated\":%s,\"logged\":%s,\"data\":{",
         (unsigned long)tick,
         updated ? "true" : "false",
         logged ? "true" : "false");
#define PRINT_FIELD(name) printf("\"" #name "\":%ld,", (long)data->name)
  PRINT_FIELD(pack_voltage_deci_volts);
  PRINT_FIELD(pack_summed_voltage_deci_volts);
  PRINT_FIELD(lowest_cell_voltage_100uv);
  PRINT_FIELD(average_cell_voltage_100uv);
  PRINT_FIELD(highest_cell_voltage_100uv);
  PRINT_FIELD(pack_current_deci_amps);
  PRINT_FIELD(lowest_pack_current_deci_amps);
  PRINT_FIELD(average_pack_current_deci_amps);
  PRINT_FIELD(highest_pack_current_deci_amps);
  PRINT_FIELD(lowest_pack_power_kw);
  PRINT_FIELD(average_pack_power_kw);
  PRINT_FIELD(highest_pack_power_kw);
  PRINT_FIELD(pack_state_of_charge);
  PRINT_FIELD(highest_temperature_c);
  PRINT_FIELD(average_temperature_c);
  PRINT_FIELD(lowest_temperature_c);
#undef PRINT_FIELD
  printf("\"fault_flags\":%u},\"request\":", data->fault_flags);
  if (dashboard_host_take_tx(&tx)) {
    printf("{\"id\":%lu,\"data\":\"", (unsigned long)tx.header.Identifier);
    for (unsigned i = 0; i < 8; i++)
      printf("%02x", tx.data[i]);
    printf("\"}");
  } else
    printf("null");
  printf(",\"labels\":{\"speed\":");
  json_label(ui_labelSpeed);
  printf(",\"drive_mode\":");
  json_label(ui_labelDriveMode);
  printf(",\"highest_temperature\":");
  json_label(ui_labelBatteryTempHigh);
  printf(",\"lowest_temperature\":");
  json_label(ui_labelBatteryTempLow);
  printf(",\"average_temperature\":");
  json_label(ui_labelBatteryTempAvg);
  printf(",\"power_unit\":");
  json_label(ui_labelPowerConsumption);
  printf("},\"status_panels\":[");
  lv_obj_t *panels[APP_STATUS_SIGNAL_COUNT] = {ui_panelIMD,
                                               ui_panelFault,
                                               ui_panelRTD,
                                               ui_panelTSAL,
                                               ui_panelBSPD,
                                               ui_panelSDC,
                                               ui_panleReadyToDrive};
  for (unsigned i = 0; i < APP_STATUS_SIGNAL_COUNT; i++) {
    printf("%s{\"hidden\":%s,\"border_rgb\":%lu}",
           i ? "," : "",
           lv_obj_has_flag(panels[i], LV_OBJ_FLAG_HIDDEN) ? "true" : "false",
           (unsigned long)(lv_color_to32(lv_obj_get_style_border_color(panels[i], LV_PART_MAIN)) &
                           0xFFFFFFU));
  }
  printf("],\"valid_mask\":%u,\"fault_count\":%lu,\"fault_pending\":%lu,\"ram_count\":%lu,\"emmc_"
         "count\":%lu,\"emmc_overflow\":%s}\n",
         data->valid_mask,
         (unsigned long)app_fault_history_count(),
         (unsigned long)app_fault_history_pending(),
         (unsigned long)app_data_log_get_count(),
         (unsigned long)status.record_count,
         status.overflow ? "true" : "false");
  fflush(stdout);
}
/**
 * Execute validated step/capture/export commands, retaining production singleton state
 * between steps.
 *
 * Inputs: stdin commands T tick speed mode valid_mask fault_mask rx_count, following frame
 * records, P path, E path or Q. Negative speed/mode mean unknown; speed is km/h, mode is
 * 0=N/1=D/2=R and masks use seven status bits. Returns 0 for orderly completion, 1 for
 * initialization/read failure and 2 for malformed commands. T orders clock -> production
 * CAN poll -> conditional RAM/eMMC logging -> battery/aux UI -> LVGL timers -> JSON. P
 * refreshes/captures; E flushes and captures the logger binary stream to a file. State
 * persists across T commands but this single-thread loop does not schedule FreeRTOS tasks.
 */
int main(void) {
  app_can_dashboard_data_t data = {0};
  app_can_set_debug_enabled(false);
  app_data_log_init();
  if (!app_can_init() || !app_emmc_log_init() || !dashboard_render_init())
    return 1;
  app_fault_history_init(1U);
  app_fault_history_attach(app_emmc_fault_read, app_emmc_fault_write, app_emmc_fault_capacity());
  app_aux_ui_state_t initial_aux = {0};
  app_ui_refresh(&data, &initial_aux, 0U);
  char line[4096];
  uint32_t last_tick = 0;
  while (fgets(line, sizeof(line), stdin)) {
    if (!strchr(line, '\n')) {
      fprintf(stderr, "Incomplete host command\n");
      return 2;
    }
    line[strcspn(line, "\r\n")] = 0;
    if (strcmp(line, "Q") == 0)
      break;
    if (line[0] == 'T' && line[1] == ' ') {
      long long tick, speed, mode, valid, fault, count;
      char *fields[6];
      if (!split_fields(line + 2, fields, 6) ||
          !parse_integer(fields[0], 10, last_tick, UINT32_MAX, &tick) ||
          !parse_integer(fields[1], 10, -1, 65535, &speed) ||
          !parse_integer(fields[2], 10, -1, 2, &mode) ||
          !parse_integer(fields[3], 10, 0, 127, &valid) ||
          !parse_integer(fields[4], 10, 0, 127, &fault) ||
          !parse_integer(fields[5], 10, 0, DASHBOARD_HOST_RX_LIMIT, &count)) {
        fprintf(stderr, "Invalid host step\n");
        return 2;
      }
      for (unsigned i = 0; i < count; i++)
        if (!read_frame(stdin)) {
          fprintf(stderr, "Invalid host frame\n");
          return 2;
        }
      last_tick = (uint32_t)tick;
      dashboard_host_set_tick(last_tick);
      bool updated = app_can_poll_dashboard_data(&data);
      bool logged = false;
      if (updated) {
        app_data_log_record(last_tick, &data);
        logged = app_emmc_log_append(last_tick, &data);
      }
      app_aux_ui_state_t aux = {0};
      aux.speed_valid = speed >= 0;
      aux.speed_kph = speed >= 0 ? (uint16_t)speed : 0;
      aux.drive_mode_valid = mode >= 0;
      aux.drive_mode = mode >= 0 ? (app_drive_mode_t)mode : APP_DRIVE_MODE_NEUTRAL;
      aux.status_inputs_initialized = true;
      for (unsigned i = 0; i < APP_STATUS_SIGNAL_COUNT; i++) {
        aux.status_signals[i].valid = (valid & (1U << i)) != 0;
        aux.status_signals[i].fault_active = (fault & (1U << i)) != 0;
      }
      app_ui_refresh(&data, &aux, last_tick);
      dashboard_render_step(last_tick);
      print_step(last_tick, updated, logged, &data);
    } else if (line[0] == 'S' && line[1] == ' ') {
      lv_obj_t *screen = strcmp(line + 2, "main") == 0     ? ui_MainScreen
                         : strcmp(line + 2, "debug") == 0  ? ui_DebugScreen
                         : strcmp(line + 2, "pack") == 0   ? ui_DetailScreen
                         : strcmp(line + 2, "faults") == 0 ? ui_FaultScreen
                                                           : NULL;
      if (screen == NULL)
        return 2;
      ui_fault_history_refresh();
      lv_disp_load_scr(screen);
      printf("{\"ok\":true}\n");
      fflush(stdout);
    } else if (line[0] == 'H' && line[1] == ' ' && line[2]) {
      FILE *file = fopen(line + 2, "w");
      bool ok = file != NULL;
      if (ok) {
        ok = fprintf(file, "sequence,boot,uptime_ms,source,state,detail\n") > 0;
        for (uint32_t i = 0; ok && i < app_fault_history_count(); ++i) {
          app_fault_event_t event;
          ok = app_fault_history_get(i, &event);
          if (ok)
            ok = fprintf(file,
                         "%lu,%lu,%lu,%s,%u,%lu\n",
                         (unsigned long)event.sequence,
                         (unsigned long)event.session_id,
                         (unsigned long)event.timestamp_ms,
                         app_fault_source_name(event.source),
                         event.state,
                         (unsigned long)event.detail) > 0;
        }
        ok = fclose(file) == 0 && ok;
      }
      printf("{\"ok\":%s}\n", ok ? "true" : "false");
      fflush(stdout);
    } else if ((line[0] == 'P' || line[0] == 'E') && line[1] == ' ' && line[2]) {
      bool ok;
      if (line[0] == 'P')
        ok = dashboard_render_capture(line + 2);
      else {
        dashboard_host_export_begin();
        ok = app_emmc_log_dump_binary() && dashboard_host_export_save(line + 2);
      }
      printf("{\"ok\":%s}\n", ok ? "true" : "false");
      fflush(stdout);
    } else {
      fprintf(stderr, "Unknown host command\n");
      return 2;
    }
  }
  bool failed = ferror(stdin) != 0;
  dashboard_render_shutdown();
  return failed ? 1 : 0;
}
