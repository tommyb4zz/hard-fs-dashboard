/* Append-only malfunction events. GUI task owns observation and reads; backend owns I/O. */
#ifndef APP_FAULT_HISTORY_H
#define APP_FAULT_HISTORY_H
#include <stdbool.h>
#include <stdint.h>
#define APP_FAULT_HISTORY_CAPACITY 256U
#define APP_FAULT_SOURCE_COUNT 32U
#define APP_FAULT_STATE_UNKNOWN 0U
#define APP_FAULT_STATE_HEALTHY 1U
#define APP_FAULT_STATE_ACTIVE 2U
/* Sources 0..9: CAN signals; 10..16: auxiliary statuses. */
#define APP_FAULT_CAN_REJECTED 17U
#define APP_FAULT_CAN_TIMEOUT 18U
#define APP_FAULT_STORAGE 19U
#define APP_FAULT_STORAGE_FULL 20U
#define APP_FAULT_SPEED 22U
#define APP_FAULT_MODE 23U

typedef struct {
  uint32_t sequence;
  uint32_t session_id;
  uint32_t timestamp_ms;
  uint16_t source;
  uint8_t state;
  uint32_t detail;
} app_fault_event_t;
/* Reader returns 1 valid, 0 erased, -1 corrupt/unavailable. Writer must verify durability. */
typedef int (*app_fault_reader_t)(uint32_t index, app_fault_event_t *event);
typedef bool (*app_fault_writer_t)(uint32_t index, const app_fault_event_t *event);
/** Initialize once per boot; repeated calls never clear history. */
void app_fault_history_init(uint32_t session_id);
/** Recover an existing journal before observing events. Never overwrites a corrupt slot. */
void app_fault_history_attach(app_fault_reader_t reader,
                              app_fault_writer_t writer,
                              uint32_t capacity);
/** Append changed state/detail; first healthy observations create no event. */
void app_fault_history_observe(uint16_t source, uint8_t state, uint32_t detail, uint32_t now_ms);
/** Retry pending events in sequence, stopping on the first failed write. */
void app_fault_history_flush(void);
/** Copy one immutable event by zero-based chronological index. */
bool app_fault_history_get(uint32_t index, app_fault_event_t *event);
/** Return retained event count (persistent + pending). */
uint32_t app_fault_history_count(void);
/** Return transitions that could not be retained because all available capacity was full. */
uint32_t app_fault_history_dropped(void);
/** Return number of current events awaiting persistent storage. */
uint32_t app_fault_history_pending(void);
/** Return true only for a usable attached persistent journal. */
bool app_fault_history_durable(void);
/** Return source name or Unknown. */
const char *app_fault_source_name(uint16_t source);
#endif
