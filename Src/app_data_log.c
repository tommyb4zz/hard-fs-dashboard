/**
 * File purpose: Implement the 4096-sample RAM ring of millisecond timestamps and complete CAN
 * dashboard snapshots. FreeRTOS critical sections protect each reset/write/counter/read;
 * oldest records are replaced after filling. CSV export copies samples individually, so
 * it is not a frozen concurrent snapshot. Persistent storage is handled by
 * app_emmc_log.c.
 */

#include "app_data_log.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define APP_DATA_LOG_CAPACITY 4096U

/* head is next physical write position; count saturates; wrapped is sticky until reset. */
typedef struct {
  app_data_log_sample_t samples[APP_DATA_LOG_CAPACITY];
  uint32_t head;
  uint32_t count;
  bool wrapped;
} app_data_log_state_t;

static app_data_log_state_t app_data_log_state = {0};

/**
 * Return zero until the RAM ring is full, then return the next write position as the
 * oldest sample index; callers read this state inside a critical section.
 *
 * The private head is the next write slot. Before saturation samples begin at physical
 * slot zero; at full count the next write slot is also the oldest retained slot. No array
 * copy occurs and the helper itself does not enter a critical section.
 */
static uint32_t app_data_log_get_oldest_index(void) {
  if (app_data_log_state.count < APP_DATA_LOG_CAPACITY) {
    return 0U;
  }

  return app_data_log_state.head;
}

/**
 * Initialize the RAM sample ring by resetting all samples and counters.
 *
 * No inputs or return value; delegate to reset and discard any current RAM history. This
 * initializes only the RAM ring, not CAN statistics or eMMC sessions.
 */
void app_data_log_init(void) {
  app_data_log_reset();
}

/**
 * Clear the RAM ring, write index, count and wrapped flag while interrupts are protected
 * by the FreeRTOS critical section.
 *
 * Zero the complete fixed-capacity state, including sample storage, head, count and wrap
 * flag. It does not clear persistent eMMC or synchronize an ongoing multi-row CSV dump as
 * one transaction.
 */
void app_data_log_reset(void) {
  taskENTER_CRITICAL();
  memset(&app_data_log_state, 0, sizeof(app_data_log_state));
  taskEXIT_CRITICAL();
}

/**
 * Copy timestamp_ms and data into the RAM ring inside a critical section, replacing the
 * oldest sample once full and setting the wrap flag; ignore null data.
 *
 * timestamp_ms is a caller-provided uint32_t millisecond value and is stored without
 * ordering checks. Copy the full snapshot by value, advance head modulo 4096 and cap count
 * at 4096. wrapped becomes true on the first overwrite after filling, not on the write
 * that initially reaches capacity. Recording does not apply measurement validation or
 * persistent storage.
 */
void app_data_log_record(uint32_t timestamp_ms, const app_can_dashboard_data_t *data) {
  if (data == NULL) {
    return;
  }

  taskENTER_CRITICAL();
  app_data_log_state.samples[app_data_log_state.head].timestamp_ms = timestamp_ms;
  memcpy(&app_data_log_state.samples[app_data_log_state.head].dashboard_data,
         data,
         sizeof(app_can_measurements_t));
  app_data_log_state.head = (app_data_log_state.head + 1U) % APP_DATA_LOG_CAPACITY;

  if (app_data_log_state.count < APP_DATA_LOG_CAPACITY) {
    app_data_log_state.count++;
  } else {
    app_data_log_state.wrapped = true;
  }
  taskEXIT_CRITICAL();
}

/**
 * Return the compile-time capacity of the RAM sample ring; no state is changed.
 *
 * Return 4096 records, independent of current occupancy. No lock is needed because this is
 * a compile-time constant and no storage is read or mutated.
 */
uint32_t app_data_log_get_capacity(void) {
  return APP_DATA_LOG_CAPACITY;
}

/**
 * Return a critical-section-protected snapshot of the retained RAM sample count.
 *
 * The return is the number of currently retained records, 0 through 4096, not total
 * lifetime writes. The critical section covers only the counter read; separately requested
 * status values are not one combined transaction.
 */
uint32_t app_data_log_get_count(void) {
  uint32_t count = 0U;

  taskENTER_CRITICAL();
  count = app_data_log_state.count;
  taskEXIT_CRITICAL();
  return count;
}

/**
 * Return a critical-section-protected snapshot indicating that a write has replaced an
 * existing sample.
 *
 * Return the sticky boolean set after overwriting a full ring and cleared by reset. It
 * describes lost oldest RAM history, not eMMC overflow or a CAN error.
 */
bool app_data_log_is_wrapped(void) {
  bool wrapped = false;

  taskENTER_CRITICAL();
  wrapped = app_data_log_state.wrapped;
  taskEXIT_CRITICAL();
  return wrapped;
}

/**
 * Copy the retained sample at index_from_oldest into sample inside a critical section;
 * return false for a null output or an index outside the current count, otherwise true.
 *
 * index_from_oldest is a zero-based logical index; convert to physical array position
 * modulo 4096 and copy both timestamp and dashboard fields. The output remains untouched
 * on false. Bounds/index/copy share one critical section, but successive calls can observe
 * different ring generations.
 */
bool app_data_log_get_sample(uint32_t index_from_oldest, app_data_log_sample_t *sample) {
  uint32_t oldest_index = 0U;
  uint32_t sample_index = 0U;

  if (sample == NULL) {
    return false;
  }

  taskENTER_CRITICAL();
  if (index_from_oldest >= app_data_log_state.count) {
    taskEXIT_CRITICAL();
    return false;
  }

  oldest_index = app_data_log_get_oldest_index();
  sample_index = (oldest_index + index_from_oldest) % APP_DATA_LOG_CAPACITY;
  *sample = app_data_log_state.samples[sample_index];
  taskEXIT_CRITICAL();
  return true;
}

/**
 * Print a CSV header and the retained RAM samples in oldest-first index order. Each sample
 * is copied separately, so concurrent recording can change the ring while the dump runs.
 *
 * Take count once, then fetch rows one by one with get_sample. Printed fields keep their
 * integer storage units and signedness; failures skip rows. printf is outside per-sample
 * critical sections, so acquisition can continue and a full-ring dump is not a coherent
 * frozen snapshot.
 */
void app_data_log_dump_csv(void) {
  app_data_log_sample_t sample = {0};
  uint32_t count = app_data_log_get_count();
  uint32_t index = 0U;

  printf("timestamp_ms,pack_voltage_deci_v,pack_summed_voltage_deci_v,"
         "lowest_cell_voltage_100uv,average_cell_voltage_100uv,highest_cell_voltage_100uv,"
         "pack_current_deci_a,lowest_pack_current_deci_a,average_pack_current_deci_a,"
         "highest_pack_current_deci_a,lowest_pack_power_kw,average_pack_power_kw,"
         "highest_pack_power_kw,pack_soc,highest_temp_c,average_temp_c,lowest_temp_c,"
         "fault_flags\r\n");

  for (index = 0U; index < count; index++) {
    if (!app_data_log_get_sample(index, &sample)) {
      continue;
    }

    printf("%lu,%u,%u,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%u\r\n",
           (unsigned long)sample.timestamp_ms,
           (unsigned int)sample.dashboard_data.pack_voltage_deci_volts,
           (unsigned int)sample.dashboard_data.pack_summed_voltage_deci_volts,
           (unsigned int)sample.dashboard_data.lowest_cell_voltage_100uv,
           (unsigned int)sample.dashboard_data.average_cell_voltage_100uv,
           (unsigned int)sample.dashboard_data.highest_cell_voltage_100uv,
           (int)sample.dashboard_data.pack_current_deci_amps,
           (int)sample.dashboard_data.lowest_pack_current_deci_amps,
           (int)sample.dashboard_data.average_pack_current_deci_amps,
           (int)sample.dashboard_data.highest_pack_current_deci_amps,
           (int)sample.dashboard_data.lowest_pack_power_kw,
           (int)sample.dashboard_data.average_pack_power_kw,
           (int)sample.dashboard_data.highest_pack_power_kw,
           (unsigned int)sample.dashboard_data.pack_state_of_charge,
           (int)sample.dashboard_data.highest_temperature_c,
           (int)sample.dashboard_data.average_temperature_c,
           (int)sample.dashboard_data.lowest_temperature_c,
           (unsigned int)sample.dashboard_data.fault_flags);
  }
}
