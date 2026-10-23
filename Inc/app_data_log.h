/**
 * File purpose: Declare timestamped full-snapshot RAM samples, ring lifecycle/counters, oldest-
 * relative reads and CSV output. Each API operation is separately protected by its
 * implementation; multiple reads or a complete dump are not guaranteed to observe one
 * ring generation. Timestamp units are milliseconds and measurement units follow
 * app_can.h.
 */

#ifndef APP_DATA_LOG_H
#define APP_DATA_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_can.h"

/* A millisecond timestamp copied from the caller plus a by-value scaled battery snapshot. */
typedef struct {
  uint32_t timestamp_ms;
  app_can_dashboard_data_t dashboard_data;
} app_data_log_sample_t;

/**
 * Initialize the RAM sample ring by resetting all samples and counters.
 *
 * No inputs or return value; delegate to reset and discard any current RAM history. This
 * initializes only the RAM ring, not CAN statistics or eMMC sessions.
 */
void app_data_log_init(void);
/**
 * Clear the RAM ring, write index, count and wrapped flag while interrupts are protected
 * by the FreeRTOS critical section.
 *
 * Zero the complete fixed-capacity state, including sample storage, head, count and wrap
 * flag. It does not clear persistent eMMC or synchronize an ongoing multi-row CSV dump as
 * one transaction.
 */
void app_data_log_reset(void);
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
void app_data_log_record(uint32_t timestamp_ms, const app_can_dashboard_data_t *data);
/**
 * Return the compile-time capacity of the RAM sample ring; no state is changed.
 *
 * Return 4096 records, independent of current occupancy. No lock is needed because this is
 * a compile-time constant and no storage is read or mutated.
 */
uint32_t app_data_log_get_capacity(void);
/**
 * Return a critical-section-protected snapshot of the retained RAM sample count.
 *
 * The return is the number of currently retained records, 0 through 4096, not total
 * lifetime writes. The critical section covers only the counter read; separately requested
 * status values are not one combined transaction.
 */
uint32_t app_data_log_get_count(void);
/**
 * Return a critical-section-protected snapshot indicating that a write has replaced an
 * existing sample.
 *
 * Return the sticky boolean set after overwriting a full ring and cleared by reset. It
 * describes lost oldest RAM history, not eMMC overflow or a CAN error.
 */
bool app_data_log_is_wrapped(void);
/**
 * Copy the retained sample at index_from_oldest into sample inside a critical section;
 * return false for a null output or an index outside the current count, otherwise true.
 *
 * index_from_oldest is a zero-based logical index; convert to physical array position
 * modulo 4096 and copy both timestamp and dashboard fields. The output remains untouched
 * on false. Bounds/index/copy share one critical section, but successive calls can observe
 * different ring generations.
 */
bool app_data_log_get_sample(uint32_t index_from_oldest, app_data_log_sample_t *sample);
/**
 * Print a CSV header and the retained RAM samples in oldest-first index order. Each sample
 * is copied separately, so concurrent recording can change the ring while the dump runs.
 *
 * Take count once, then fetch rows one by one with get_sample. Printed fields keep their
 * integer storage units and signedness; failures skip rows. printf is outside per-sample
 * critical sections, so acquisition can continue and a full-ring dump is not a coherent
 * frozen snapshot.
 */
void app_data_log_dump_csv(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_DATA_LOG_H */
