/*
 * Turn live dashboard inputs into append-only malfunction episodes: one entry when a problem
 * starts (or changes kind) and one CLEAR entry once it has stayed away for a hold time.
 * Counters are summarized per episode instead of being written on every increment, and
 * inputs without a provider are never recorded. GUI task only; history owns persistence.
 */
#ifndef APP_FAULT_MONITOR_H
#define APP_FAULT_MONITOR_H
#include <stdbool.h>
#include <stdint.h>
#include "app_can.h"
#include "app_dashboard_ui.h"
#include "app_emmc_log.h"
#include "app_fault_history.h"

/* Missing inputs are not recorded until boot has had this long to connect. */
#define APP_FAULT_STARTUP_GRACE_MS 10000U
/* A recovered condition must stay healthy this long before its CLEAR is recorded. */
#define APP_FAULT_CLEAR_HOLD_MS 5000U
/* Error-counter episodes (timeouts, rejected frames, storage errors) end after this quiet time. */
#define APP_FAULT_ERROR_QUIET_MS 60000U

/* UNKNOWN details for CAN signals. */
#define APP_FAULT_SIGNAL_NEVER_RECEIVED 0U
#define APP_FAULT_SIGNAL_STOPPED 1U
/* ACTIVE detail for CAN signals: limit kind in bits 16..23, raw 16-bit value below. */
#define APP_FAULT_LIMIT_ABOVE 1U
#define APP_FAULT_LIMIT_BELOW 2U
/* ACTIVE details for the BMS link. */
#define APP_FAULT_LINK_NEVER_CONNECTED 0U
#define APP_FAULT_LINK_LOST 1U
/* ACTIVE details for telemetry storage. */
#define APP_FAULT_STORAGE_NOT_READY 1U
#define APP_FAULT_STORAGE_WRITE_ERRORS 2U

/* Optional limit in the signal's native units from app_can.h (0.1 V, 100 uV, 0.1 A, C, %). */
typedef struct {
  bool enabled;
  int32_t minimum;
  int32_t maximum;
} app_fault_limit_t;

/**
 * Replace the per-signal limits. All are disabled by default: limits must come from the pack
 * and BMS specification, never from signal names. NULL disables every limit.
 */
void app_fault_monitor_set_limits(const app_fault_limit_t limits[APP_CAN_SIGNAL_COUNT]);
/**
 * Evaluate one refresh and append episode starts/ends to app_fault_history.
 *
 * can is the latest snapshot, aux the auxiliary inputs with their wired flags, storage the
 * telemetry logger status and now_ms the same millisecond clock as the snapshot ages.
 */
void app_fault_monitor_update(const app_can_dashboard_data_t *can,
                              const app_aux_ui_state_t *aux,
                              const app_emmc_log_status_t *storage,
                              uint32_t now_ms);
/** Write a short readable description of one event (source, meaning of state and detail). */
void app_fault_describe(const app_fault_event_t *event, char *text, uint32_t size);
#endif
