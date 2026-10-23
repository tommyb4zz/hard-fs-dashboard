/**
 * File purpose: Create and run the FreeRTOS GUI, CAN acquisition and UART console tasks. A
 * one-element overwrite queue transfers the latest full battery snapshot to the GUI, while
 * acquisition records snapshots in RAM/eMMC. The GUI owns LVGL and uses synthetic
 * auxiliary placeholders. Console commands inspect/reset/export logs. This coordination
 * does not bind real speed/mode/status inputs or recover failed peripherals.
 */

#include "app_tasks.h"

#include <stdio.h>
#include <string.h>

#include "app_can.h"
#include "app_dashboard_ui.h"
#include "app_data_log.h"
#include "app_emmc_log.h"
#include "main.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "lvgl/lvgl.h"
#include "lvgl_port_lcd.h"
#include "lvgl_port_touchpad.h"
#include "ui/screens/ui_MainScreen.h"
#include "ui/ui.h"

/* Queue length is one: acquisition overwrites old UI work so the GUI sees latest state. */
static QueueHandle_t app_ui_queue = NULL;

/* Initial zeros are display defaults, not established battery measurements. */
static const app_can_dashboard_data_t app_default_dashboard_data = {
    .pack_voltage_deci_volts = 0U,
    .pack_summed_voltage_deci_volts = 0U,
    .lowest_cell_voltage_100uv = 0U,
    .average_cell_voltage_100uv = 0U,
    .highest_cell_voltage_100uv = 0U,
    .pack_current_deci_amps = 0,
    .lowest_pack_current_deci_amps = 0,
    .average_pack_current_deci_amps = 0,
    .highest_pack_current_deci_amps = 0,
    .lowest_pack_power_kw = 0,
    .average_pack_power_kw = 0,
    .highest_pack_power_kw = 0,
    .pack_state_of_charge = 0U,
    .highest_temperature_c = 0,
    .average_temperature_c = 0,
    .lowest_temperature_c = 0,
    .fault_flags = 0U,
};

/* Stack sizes are FreeRTOS words; periods are milliseconds converted with pdMS_TO_TICKS.
 * GUI/console priority is idle+1; acquisition idle+2. No task handles are retained. */
#define APP_GUI_TASK_STACK_WORDS 2048U
#define APP_GUI_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)
#define APP_GUI_TASK_PERIOD_MS 5U

#define APP_CAN_TASK_STACK_WORDS 1024U
#define APP_CAN_TASK_PRIORITY (tskIDLE_PRIORITY + 2U)
#define APP_CAN_TASK_POLL_MS 10U
#define APP_CAN_RETRY_MS 1000U

#define APP_CONSOLE_TASK_STACK_WORDS 1024U
#define APP_CONSOLE_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)
#define APP_CONSOLE_TASK_POLL_MS 5U
#define APP_CONSOLE_UART_RX_TIMEOUT_MS 50U

/**
 * Print the supported single-character UART console commands to the configured printf
 * output.
 *
 * No inputs or return value; one printf lists the implemented character-to-command
 * mapping. It neither executes those operations nor guarantees exclusive UART ownership.
 */
static void app_console_print_help(void) {
  printf("[console] commands: h=help i=ram-log-info d=ram-dump c=ram-clear "
         "e=emmc-info p=emmc-dump-csv b=script-bin-dump x=emmc-reset "
         "f=emmc-flush v=toggle-can-debug\r\n");
}

/**
 * Read RAM log counters and print count, capacity and wrap status to the console.
 *
 * No inputs; call independent protected getters and format decimal counts plus a 0/1 wrap
 * flag. Acquisition can occur between getter calls, so this is informational output rather
 * than a transaction snapshot.
 */
static void app_console_print_log_info(void) {
  printf("[console] log count=%lu capacity=%lu wrapped=%u\r\n",
         (unsigned long)app_data_log_get_count(),
         (unsigned long)app_data_log_get_capacity(),
         app_data_log_is_wrapped() ? 1U : 0U);
}

/**
 * Read the eMMC logger status snapshot and print readiness, session, record/block counts
 * and overflow state.
 *
 * Get one mutex-protected status structure then print the ready/overflow booleans and
 * session/block/record counts. The block denominator includes the reserved metadata block;
 * printing occurs after the status lock is released.
 */
static void app_console_print_emmc_log_info(void) {
  app_emmc_log_status_t status = {0};

  app_emmc_log_get_status(&status);
  printf("[console] emmc ready=%u session=%lu records=%lu blocks=%lu/%lu overflow=%u\r\n",
         status.ready ? 1U : 0U,
         (unsigned long)status.session_id,
         (unsigned long)status.record_count,
         (unsigned long)status.written_blocks,
         (unsigned long)status.reserved_blocks,
         status.overflow ? 1U : 0U);
}

/**
 * Dispatch one UART command to log inspection, dump, clear/reset/flush or CAN debug
 * control. Binary/CSV eMMC dumps temporarily suppress CAN debug output and restore it
 * afterward; unknown commands print help.
 *
 * command is a single received byte; upper/lowercase letters share actions, CR/LF do
 * nothing. RAM dumps/clear, eMMC info/flush/reset/CSV/binary and diagnostic toggle have
 * their switch-defined side effects. CSV/binary temporarily change the global CAN debug
 * setting but provide no global UART output lock. Binary return status is deliberately
 * ignored; flush/reset commands print their boolean result.
 */
static void app_console_handle_command(uint8_t command) {
  bool restore_can_debug = false;
  bool can_debug_was_enabled = false;

  switch (command) {
  case 'h':
  case 'H':
  case '?':
    app_console_print_help();
    break;

  case 'i':
  case 'I':
    app_console_print_log_info();
    break;

  case 'd':
  case 'D':
    printf("[console] dump begin\r\n");
    app_data_log_dump_csv();
    printf("[console] dump end\r\n");
    break;

  case 'c':
  case 'C':
    app_data_log_reset();
    printf("[console] log cleared\r\n");
    break;

  case 'e':
  case 'E':
    app_console_print_emmc_log_info();
    break;

  case 'f':
  case 'F':
    if (app_emmc_log_flush()) {
      printf("[console] emmc flush ok\r\n");
    } else {
      printf("[console] emmc flush failed\r\n");
    }
    break;

  case 'x':
  case 'X':
    if (app_emmc_log_reset_session()) {
      printf("[console] emmc session reset\r\n");
      app_console_print_emmc_log_info();
    } else {
      printf("[console] emmc session reset failed\r\n");
    }
    break;

  case 'p':
  case 'P':
    can_debug_was_enabled = app_can_is_debug_enabled();
    if (can_debug_was_enabled) {
      app_can_set_debug_enabled(false);
      restore_can_debug = true;
    }
    printf("[console] emmc dump begin\r\n");
    app_emmc_log_dump_csv();
    printf("[console] emmc dump end\r\n");
    if (restore_can_debug) {
      app_can_set_debug_enabled(true);
    }
    break;

  case 'b':
  case 'B':
    can_debug_was_enabled = app_can_is_debug_enabled();
    if (can_debug_was_enabled) {
      app_can_set_debug_enabled(false);
      restore_can_debug = true;
    }
    (void)app_emmc_log_dump_binary();
    if (restore_can_debug) {
      app_can_set_debug_enabled(true);
    }
    break;

  case 'v':
  case 'V':
    app_can_set_debug_enabled(!app_can_is_debug_enabled());
    printf("[console] can debug=%u\r\n", app_can_is_debug_enabled() ? 1U : 0U);
    break;

  case '\r':
  case '\n':
    break;

  default:
    printf("[console] unknown command '%c'\r\n", (char)command);
    app_console_print_help();
    break;
  }
}

/**
 * FreeRTOS entry point with an unused argument: initialize LVGL, LCD, touch and screens,
 * then consume the latest CAN snapshot, update auxiliary placeholders and service LVGL on
 * the configured 5 ms schedule; never return.
 *
 * argument is unused. The task alone owns normal LVGL calls: initialize
 * library/display/touch/generated objects before any binding, drain the queue without
 * waiting, and apply auxiliary changes only when the full state differs. It advances LVGL
 * time by a fixed 5 ms per loop rather than measured elapsed time, services timers, then
 * delays against last_wake. Queue overwrites can omit intermediate UI states; LCD fatal
 * errors halt, touch initialization can silently return.
 */
static void app_gui_task(void *argument) {
  app_aux_ui_state_t aux_state = {0};
  app_aux_ui_state_t last_applied_aux_state = {0};
  app_can_dashboard_data_t data = app_default_dashboard_data;
  TickType_t last_wake = xTaskGetTickCount();
  bool aux_state_applied = false;
  uint32_t now_ms = 0U;

  (void)argument;

  lv_init();
  LCD_init();
  touchpad_init();
  ui_init();
  app_ui_apply_dashboard_data(&data);
  app_ui_update_aux_placeholders(&aux_state, 0U);
  app_ui_apply_aux_state(&aux_state, 0U);
  last_applied_aux_state = aux_state;
  aux_state_applied = true;

  for (;;) {
    while (xQueueReceive(app_ui_queue, &data, 0U) == pdPASS) {
      app_ui_apply_dashboard_data(&data);
    }

    now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    app_ui_update_aux_placeholders(&aux_state, now_ms);
    if (!aux_state_applied ||
        (memcmp(&aux_state, &last_applied_aux_state, sizeof(aux_state)) != 0)) {
      app_ui_apply_aux_state(&aux_state, now_ms);
      last_applied_aux_state = aux_state;
      aux_state_applied = true;
    }

    lv_tick_inc(APP_GUI_TASK_PERIOD_MS);
    lv_timer_handler();
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_GUI_TASK_PERIOD_MS));
  }
}

/**
 * FreeRTOS entry point with an unused argument: retry CAN initialization, poll received
 * dashboard updates, timestamp and record each update in RAM/eMMC, and overwrite the one-
 * element UI queue; delay between polls and never return.
 *
 * argument is unused. Retry initialization at 1000 ms on failure; after readiness poll
 * every 10 ms. One true poll can aggregate several accepted responses into one latest
 * snapshot, recorded with the FreeRTOS tick-derived millisecond timestamp in RAM and eMMC
 * before queue overwrite. eMMC append and queue results are ignored, so task output does
 * not establish persistent success. CAN priority is one level above GUI/console.
 */
static void app_can_rx_task(void *argument) {
  app_can_dashboard_data_t data = {0};
  bool can_ready = false;
  uint32_t timestamp_ms = 0U;
  bool init_attempt_logged = false;

  (void)argument;

  if (app_can_is_debug_enabled()) {
    printf("[can] task start\r\n");
  }

  for (;;) {
    if (!can_ready) {
      if (app_can_is_debug_enabled() && !init_attempt_logged) {
        printf("[can] init attempt\r\n");
        init_attempt_logged = true;
      }
      can_ready = app_can_init();
      if (!can_ready) {
        vTaskDelay(pdMS_TO_TICKS(APP_CAN_RETRY_MS));
        continue;
      }

      init_attempt_logged = false;
    }

    if (app_can_poll_dashboard_data(&data)) {
      timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
      app_data_log_record(timestamp_ms, &data);
      (void)app_emmc_log_append(timestamp_ms, &data);
      (void)xQueueOverwrite(app_ui_queue, &data);
    }

    vTaskDelay(pdMS_TO_TICKS(APP_CAN_TASK_POLL_MS));
  }
}

/**
 * FreeRTOS entry point with an unused argument: print initial log/help status, receive
 * single bytes from COM1 and execute console commands; echo non-binary-dump commands,
 * delay between reads and never return.
 *
 * argument is unused. HAL_UART_Receive reads one byte with a 50 ms timeout, and only
 * HAL_OK dispatches. Echo printable ASCII normally, but omit echo for b/B to keep the
 * binary header first; UART failures/timeouts are ignored before the 5 ms task delay.
 * Dumps execute synchronously inside this task and can delay future commands.
 */
static void app_console_task(void *argument) {
  uint8_t command = 0U;
  HAL_StatusTypeDef uart_status = HAL_OK;

  (void)argument;

  printf("[console] task start\r\n");
  app_console_print_help();
  app_console_print_log_info();
  app_console_print_emmc_log_info();

  for (;;) {
    uart_status = HAL_UART_Receive(&hcom_uart[COM1], &command, 1U, APP_CONSOLE_UART_RX_TIMEOUT_MS);
    if (uart_status == HAL_OK) {
      if ((command != 'b') && (command != 'B')) {
        printf("[console] rx=0x%02X '%c'\r\n",
               (unsigned int)command,
               ((command >= 0x20U) && (command <= 0x7EU)) ? (char)command : '.');
      }
      app_console_handle_command(command);
    }

    vTaskDelay(pdMS_TO_TICKS(APP_CONSOLE_TASK_POLL_MS));
  }
}

/**
 * Initialize the loggers, allocate the one-element dashboard queue and create GUI, CAN and
 * console tasks; return false on queue/task allocation failure, otherwise true. eMMC
 * initialization failure does not stop task creation.
 *
 * Call before starting the scheduler. RAM initialization is unconditional and eMMC failure
 * is ignored; allocate a queue holding exactly one full snapshot, then GUI/CAN/console
 * tasks in that order. Stack sizes are FreeRTOS words (2048/1024/1024), not bytes. False
 * returns on the first allocation failure without deleting resources/tasks already
 * created. The function starts neither the scheduler nor a physical input binding.
 */
bool app_tasks_start(void) {
  app_data_log_init();
  (void)app_emmc_log_init();

  app_ui_queue = xQueueCreate(1U, sizeof(app_can_dashboard_data_t));
  if (app_ui_queue == NULL) {
    return false;
  }

  if (xTaskCreate(
          app_gui_task, "gui", APP_GUI_TASK_STACK_WORDS, NULL, APP_GUI_TASK_PRIORITY, NULL) !=
      pdPASS) {
    return false;
  }

  if (xTaskCreate(
          app_can_rx_task, "can_rx", APP_CAN_TASK_STACK_WORDS, NULL, APP_CAN_TASK_PRIORITY, NULL) !=
      pdPASS) {
    return false;
  }

  if (xTaskCreate(app_console_task,
                  "console",
                  APP_CONSOLE_TASK_STACK_WORDS,
                  NULL,
                  APP_CONSOLE_TASK_PRIORITY,
                  NULL) != pdPASS) {
    return false;
  }

  return true;
}
