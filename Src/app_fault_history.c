/* One owner, append-only event journal; no erase/reset API and no overwriting old events. */
#include "app_fault_history.h"
#include <string.h>
static app_fault_event_t pending[APP_FAULT_HISTORY_CAPACITY];
static uint32_t pending_count, stored_count, dropped, journal_capacity, session;
static uint8_t last_state[APP_FAULT_SOURCE_COUNT];
static uint32_t last_detail[APP_FAULT_SOURCE_COUNT];
static bool observed[APP_FAULT_SOURCE_COUNT], initialized, blocked, write_failed;
static app_fault_reader_t read_event;
static app_fault_writer_t write_event;

/** Initialize a boot's observation state without offering a way to delete records. */
void app_fault_history_init(uint32_t session_id) {
  if (initialized)
    return;
  initialized = true;
  session = session_id == 0U ? 1U : session_id;
}
/** Reject records whose checksum passed but whose fields this firmware cannot interpret. */
static bool app_fault_event_shape_valid(const app_fault_event_t *event, uint32_t index) {
  return event->sequence == index + 1U && event->source < APP_FAULT_SOURCE_COUNT &&
         event->state <= APP_FAULT_STATE_ACTIVE;
}
/**
 * Find the end of the written prefix by binary search, then check the last record.
 *
 * Appends are strictly sequential, so slots before the end are occupied and slots after it
 * are erased; boot cost is O(log capacity) reads instead of one read per stored event. A
 * corrupt final slot is a torn last write and is skipped, never rewritten. Any other
 * inconsistency blocks new writes so the evidence stays as found. An I/O error leaves the
 * journal detached for this boot; events are then kept in RAM.
 */
void app_fault_history_attach(app_fault_reader_t reader,
                              app_fault_writer_t writer,
                              uint32_t capacity) {
  if (read_event != NULL || stored_count != 0U || pending_count != 0U || reader == NULL ||
      writer == NULL || capacity == 0U)
    return;
  app_fault_history_init(1U);
  app_fault_event_t event;
  uint32_t low = 0U, high = capacity;
  while (low < high) {
    uint32_t middle = low + (high - low) / 2U;
    int result = reader(middle, &event);
    if (result == APP_FAULT_SLOT_IO_ERROR)
      return;
    if (result == APP_FAULT_SLOT_ERASED)
      high = middle;
    else
      low = middle + 1U;
  }
  bool consistent = true, have_session = false;
  uint32_t last_session = 0U;
  if (low > 0U) {
    int result = reader(low - 1U, &event);
    if (result == APP_FAULT_SLOT_IO_ERROR)
      return;
    if (result == APP_FAULT_SLOT_VALID) {
      consistent = app_fault_event_shape_valid(&event, low - 1U);
      have_session = consistent;
      last_session = event.session_id;
    } else if (low > 1U) {
      result = reader(low - 2U, &event);
      if (result == APP_FAULT_SLOT_IO_ERROR)
        return;
      consistent = result == APP_FAULT_SLOT_VALID && app_fault_event_shape_valid(&event, low - 2U);
      have_session = consistent;
      last_session = event.session_id;
    }
  }
  read_event = reader;
  write_event = writer;
  journal_capacity = capacity;
  stored_count = low;
  if (!consistent || (have_session && last_session == UINT32_MAX))
    blocked = true;
  else if (have_session && last_session >= session)
    session = last_session + 1U;
}
/** Flush queued immutable records, leaving failed and later events intact for retry. */
void app_fault_history_flush(void) {
  if (write_event == NULL || blocked)
    return;
  while (pending_count != 0U && stored_count < journal_capacity) {
    if (!write_event(stored_count, &pending[0])) {
      write_failed = true;
      return;
    }
    write_failed = false;
    stored_count++;
    pending_count--;
    memmove(pending, pending + 1, pending_count * sizeof(pending[0]));
  }
}
/** Record state/detail transitions once, with copied immutable values and boot-relative time. */
void app_fault_history_observe(uint16_t source, uint8_t state, uint32_t detail, uint32_t now_ms) {
  app_fault_history_init(1U);
  if (source >= APP_FAULT_SOURCE_COUNT || state > APP_FAULT_STATE_ACTIVE)
    return;
  if (observed[source] && last_state[source] == state && last_detail[source] == detail)
    return;
  bool first_healthy = !observed[source] && state == APP_FAULT_STATE_HEALTHY;
  observed[source] = true;
  last_state[source] = state;
  last_detail[source] = detail;
  if (first_healthy)
    return;
  app_fault_history_flush();
  /* A full journal still shows this boot's events from RAM; only a full RAM queue drops. */
  if (pending_count == APP_FAULT_HISTORY_CAPACITY) {
    if (dropped != UINT32_MAX)
      dropped++;
    return;
  }
  pending[pending_count] = (app_fault_event_t){.sequence = stored_count + pending_count + 1U,
                                               .session_id = session,
                                               .timestamp_ms = now_ms,
                                               .source = source,
                                               .state = state,
                                               .detail = detail};
  pending_count++;
  app_fault_history_flush();
}
/** Copy by index; callers can never edit the journal through a returned pointer. */
bool app_fault_history_get(uint32_t index, app_fault_event_t *event) {
  if (event == NULL || index >= stored_count + pending_count)
    return false;
  if (index < stored_count) {
    app_fault_event_t copy;
    if (read_event(index, &copy) != APP_FAULT_SLOT_VALID ||
        !app_fault_event_shape_valid(&copy, index))
      return false;
    *event = copy;
    return true;
  }
  *event = pending[index - stored_count];
  return true;
}
/** Return immutable history length. */
uint32_t app_fault_history_count(void) {
  return stored_count + pending_count;
}
/** Report capacity loss explicitly. */
uint32_t app_fault_history_dropped(void) {
  return dropped;
}
/** Report events that are still volatile. */
uint32_t app_fault_history_pending(void) {
  return pending_count;
}
/** Explain persistence in priority order; full journals cannot promise future retention. */
app_fault_journal_state_t app_fault_history_journal_state(void) {
  if (write_event == NULL)
    return APP_FAULT_JOURNAL_NO_STORAGE;
  if (blocked)
    return APP_FAULT_JOURNAL_BLOCKED;
  if (stored_count >= journal_capacity)
    return APP_FAULT_JOURNAL_FULL;
  return write_failed ? APP_FAULT_JOURNAL_WRITE_FAILED : APP_FAULT_JOURNAL_SAVED;
}
/** Report persistence availability. */
bool app_fault_history_durable(void) {
  return app_fault_history_journal_state() == APP_FAULT_JOURNAL_SAVED;
}
/** Return this boot's ID after journal recovery has advanced it past saved boots. */
uint32_t app_fault_history_session(void) {
  return session;
}
/** Name diagnostics without inventing vehicle wiring or CAN fields. */
const char *app_fault_source_name(uint16_t source) {
  static const char *const names[APP_FAULT_SOURCE_COUNT] = {"Pack voltage",
                                                            "Summed voltage",
                                                            "Pack current",
                                                            "State of charge",
                                                            "High temperature",
                                                            "Low temperature",
                                                            "Avg temperature",
                                                            "Low cell voltage",
                                                            "High cell voltage",
                                                            "Avg cell voltage",
                                                            "IMD",
                                                            "FAULT",
                                                            "RTD",
                                                            "TSAL",
                                                            "BSPD",
                                                            "SDC",
                                                            "Ready to drive",
                                                            "CAN rejected frames",
                                                            "CAN request timeout",
                                                            "Storage unavailable",
                                                            "Storage full",
                                                            "History storage",
                                                            "Speed input",
                                                            "Drive mode input",
                                                            "BMS link",
                                                            "Unknown",
                                                            "Unknown",
                                                            "Unknown",
                                                            "Unknown",
                                                            "Unknown",
                                                            "Unknown",
                                                            "Unknown"};
  return source < APP_FAULT_SOURCE_COUNT ? names[source] : "Unknown";
}
