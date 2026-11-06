/* Append-only malfunction events. GUI task owns observation and reads; backend owns I/O. */
#ifndef APP_FAULT_HISTORY_H
#define APP_FAULT_HISTORY_H
#include <stdbool.h>
#include <stdint.h>
/* RAM queue for events not yet saved (no card, failing writes, blocked or full journal). */
#define APP_FAULT_HISTORY_CAPACITY 256U
#define APP_FAULT_SOURCE_COUNT 32U
#define APP_FAULT_STATE_UNKNOWN 0U
#define APP_FAULT_STATE_HEALTHY 1U
#define APP_FAULT_STATE_ACTIVE 2U
/* Sources 0..9: CAN signals (app_can_signal_id_t); 10..16: auxiliary statuses. */
#define APP_FAULT_AUX_STATUS_FIRST 10U
#define APP_FAULT_CAN_REJECTED 17U
#define APP_FAULT_CAN_TIMEOUT 18U
#define APP_FAULT_STORAGE 19U
#define APP_FAULT_STORAGE_FULL 20U
#define APP_FAULT_HISTORY_STORAGE 21U
#define APP_FAULT_SPEED 22U
#define APP_FAULT_MODE 23U
#define APP_FAULT_BMS_LINK 24U

/* Backend slot read results. An I/O error is not evidence about slot contents. */
#define APP_FAULT_SLOT_VALID 1
#define APP_FAULT_SLOT_ERASED 0
#define APP_FAULT_SLOT_CORRUPT (-1)
#define APP_FAULT_SLOT_IO_ERROR (-2)

/* Why new events are or are not being saved; also the detail of History storage events. */
typedef enum {
  APP_FAULT_JOURNAL_SAVED = 0,
  APP_FAULT_JOURNAL_NO_STORAGE,
  APP_FAULT_JOURNAL_WRITE_FAILED,
  APP_FAULT_JOURNAL_BLOCKED,
  APP_FAULT_JOURNAL_FULL
} app_fault_journal_state_t;

typedef struct {
  uint32_t sequence;
  uint32_t session_id;
  uint32_t timestamp_ms;
  uint16_t source;
  uint8_t state;
  uint32_t detail;
} app_fault_event_t;
/* Reader returns one APP_FAULT_SLOT_* value. Writer must verify durability before true. */
typedef int (*app_fault_reader_t)(uint32_t index, app_fault_event_t *event);
typedef bool (*app_fault_writer_t)(uint32_t index, const app_fault_event_t *event);
/** Initialize once per boot with a boot ID hint; repeated calls never clear history. */
void app_fault_history_init(uint32_t session_id);
/** Attach an existing journal before observing events. Never overwrites an occupied slot. */
void app_fault_history_attach(app_fault_reader_t reader,
                              app_fault_writer_t writer,
                              uint32_t capacity);
/** Append changed state/detail; first healthy observations create no event. */
void app_fault_history_observe(uint16_t source, uint8_t state, uint32_t detail, uint32_t now_ms);
/** Retry pending events in sequence, stopping on the first failed write. */
void app_fault_history_flush(void);
/** Copy one immutable event by zero-based chronological index. */
bool app_fault_history_get(uint32_t index, app_fault_event_t *event);
/** Return retained event count (saved slots + RAM events). */
uint32_t app_fault_history_count(void);
/** Return events that could not be kept at all because the RAM queue was full. */
uint32_t app_fault_history_dropped(void);
/** Return number of current events that exist only in RAM. */
uint32_t app_fault_history_pending(void);
/** Report whether new events are being saved, and if not, why. */
app_fault_journal_state_t app_fault_history_journal_state(void);
/** Return true only while new events are being saved to an attached journal. */
bool app_fault_history_durable(void);
/** Return the boot ID stamped on events recorded during this boot. */
uint32_t app_fault_history_session(void);
/** Return source name or Unknown. */
const char *app_fault_source_name(uint16_t source);
#endif
