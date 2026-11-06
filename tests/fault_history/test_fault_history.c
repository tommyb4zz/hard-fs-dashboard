/* Exercise the production append-only malfunction ledger in separate fresh processes. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_fault_history.h"

#define CHECK(condition)                                                                           \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #condition);                         \
      return false;                                                                                \
    }                                                                                              \
  } while (0)

/** Compare every field of two copied events. */
static bool same_event(const app_fault_event_t *left, const app_fault_event_t *right) {
  return left->sequence == right->sequence && left->session_id == right->session_id &&
         left->timestamp_ms == right->timestamp_ms && left->source == right->source &&
         left->state == right->state && left->detail == right->detail;
}

/* Recovery and recurrence append records instead of editing or deleting their predecessors. */
static bool lifecycle(void) {
  app_fault_event_t first = {0}, event = {0};
  app_fault_history_init(17U);
  CHECK(app_fault_history_count() == 0U);
  CHECK(app_fault_history_dropped() == 0U);
  CHECK(!app_fault_history_get(0U, &event));

  app_fault_history_observe(0U, APP_FAULT_STATE_HEALTHY, 0U, 100U);
  CHECK(app_fault_history_count() == 0U);
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 0x80U, 123U);
  CHECK(app_fault_history_count() == 1U);
  CHECK(app_fault_history_get(0U, &first));
  CHECK(first.sequence == 1U && first.session_id == 17U && first.timestamp_ms == 123U);
  CHECK(first.source == 0U && first.state == APP_FAULT_STATE_ACTIVE && first.detail == 0x80U);

  for (uint32_t tick = 124U; tick < 224U; tick++) {
    app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 0x80U, tick);
  }
  CHECK(app_fault_history_count() == 1U);
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 0x81U, 224U);
  CHECK(app_fault_history_count() == 2U);
  CHECK(app_fault_history_get(1U, &event));
  CHECK(event.sequence == 2U && event.detail == 0x81U && event.timestamp_ms == 224U);

  app_fault_history_observe(0U, APP_FAULT_STATE_UNKNOWN, 9U, 300U);
  app_fault_history_observe(0U, APP_FAULT_STATE_HEALTHY, 0U, 400U);
  app_fault_history_observe(0U, APP_FAULT_STATE_HEALTHY, 0U, 401U);
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 0x80U, 500U);
  CHECK(app_fault_history_count() == 5U);
  CHECK(app_fault_history_get(2U, &event));
  CHECK(event.state == APP_FAULT_STATE_UNKNOWN && event.timestamp_ms == 300U);
  CHECK(app_fault_history_get(3U, &event));
  CHECK(event.state == APP_FAULT_STATE_HEALTHY && event.timestamp_ms == 400U);
  CHECK(app_fault_history_get(4U, &event));
  CHECK(event.state == APP_FAULT_STATE_ACTIVE && event.timestamp_ms == 500U);

  app_fault_history_init(999U); /* Reinitialization cannot erase or replace the live session. */
  CHECK(app_fault_history_count() == 5U);
  app_fault_history_observe(0U, APP_FAULT_STATE_HEALTHY, 0U, 600U);
  CHECK(app_fault_history_get(5U, &event));
  CHECK(event.sequence == 6U && event.session_id == 17U);
  CHECK(app_fault_history_get(0U, &event));
  CHECK(same_event(&first, &event));
  event.sequence = 999U;
  event.detail = 0U;
  CHECK(app_fault_history_get(0U, &event));
  CHECK(same_event(&first, &event));
  return true;
}

/* Each source retains independent state; invalid inputs cannot consume history capacity. */
static bool guards_and_source_isolation(void) {
  app_fault_event_t event = {0};
  app_fault_history_init(1U);
  app_fault_history_observe(APP_FAULT_SOURCE_COUNT, APP_FAULT_STATE_ACTIVE, 0U, 0U);
  app_fault_history_observe(UINT16_MAX, APP_FAULT_STATE_ACTIVE, 0U, 0U);
  app_fault_history_observe(0U, 3U, 0U, 0U);
  app_fault_history_observe(0U, UINT8_MAX, 0U, 0U);
  CHECK(app_fault_history_count() == 0U);
  CHECK(app_fault_history_dropped() == 0U);
  CHECK(!app_fault_history_get(0U, NULL));
  CHECK(!app_fault_history_get(UINT32_MAX, &event));

  app_fault_history_observe(0U, APP_FAULT_STATE_HEALTHY, 0U, 0U);
  app_fault_history_observe(1U, APP_FAULT_STATE_HEALTHY, 0U, 0U);
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 2U, 1U);
  app_fault_history_observe(1U, APP_FAULT_STATE_ACTIVE, 2U, 1U);
  CHECK(app_fault_history_count() == 2U);
  app_fault_history_observe(0U, APP_FAULT_STATE_HEALTHY, 0U, 2U);
  app_fault_history_observe(1U, APP_FAULT_STATE_ACTIVE, 2U, 2U);
  CHECK(app_fault_history_count() == 3U);
  CHECK(app_fault_history_get(1U, &event));
  CHECK(event.source == 1U && event.state == APP_FAULT_STATE_ACTIVE);
  CHECK(app_fault_history_get(2U, &event));
  CHECK(event.source == 0U && event.state == APP_FAULT_STATE_HEALTHY);

  app_fault_history_observe(APP_FAULT_SOURCE_COUNT - 1U, APP_FAULT_STATE_UNKNOWN, 0U, 3U);
  app_fault_history_observe(APP_FAULT_SOURCE_COUNT - 1U, APP_FAULT_STATE_UNKNOWN, 0U, 4U);
  CHECK(app_fault_history_count() == 4U);
  CHECK(app_fault_history_get(3U, &event));
  CHECK(event.source == APP_FAULT_SOURCE_COUNT - 1U && event.state == APP_FAULT_STATE_UNKNOWN);
  CHECK(event.timestamp_ms == 3U);
  CHECK(!app_fault_history_get(4U, &event));
  CHECK(!app_fault_history_get(0U, NULL));
  return true;
}

/* A full RAM-only ledger keeps every existing entry intact and explicitly counts losses. */
static bool overflow_never_overwrites(void) {
  app_fault_event_t retained[APP_FAULT_HISTORY_CAPACITY];
  app_fault_event_t event = {0};
  app_fault_history_init(77U);
  for (uint32_t i = 0U; i < APP_FAULT_HISTORY_CAPACITY; i++) {
    uint8_t state = (i % 2U == 0U) ? APP_FAULT_STATE_ACTIVE : APP_FAULT_STATE_HEALTHY;
    app_fault_history_observe(0U, state, 0U, i);
    CHECK(app_fault_history_get(i, &retained[i]));
    CHECK(retained[i].sequence == i + 1U && retained[i].timestamp_ms == i);
  }
  CHECK(app_fault_history_count() == APP_FAULT_HISTORY_CAPACITY);
  CHECK(app_fault_history_dropped() == 0U);

  for (uint32_t i = 0U; i < 8U; i++) {
    uint8_t state = (i % 2U == 0U) ? APP_FAULT_STATE_ACTIVE : APP_FAULT_STATE_HEALTHY;
    app_fault_history_observe(0U, state, 0U, APP_FAULT_HISTORY_CAPACITY + i);
    CHECK(app_fault_history_dropped() == i + 1U);
    app_fault_history_observe(0U, state, 0U, APP_FAULT_HISTORY_CAPACITY + i + 100U);
    CHECK(app_fault_history_dropped() == i + 1U); /* Refreshes do not inflate lost count. */
  }
  CHECK(app_fault_history_count() == APP_FAULT_HISTORY_CAPACITY);
  CHECK(!app_fault_history_get(APP_FAULT_HISTORY_CAPACITY, &event));
  for (uint32_t i = 0U; i < APP_FAULT_HISTORY_CAPACITY; i++) {
    CHECK(app_fault_history_get(i, &event));
    CHECK(same_event(&retained[i], &event));
  }
  return true;
}

/* This backend models immutable verified slots, erased slots and unreadable occupied slots. */
#define DISK_CAPACITY 8U
static app_fault_event_t disk[DISK_CAPACITY];
static int disk_state[DISK_CAPACITY];
static uint32_t successful_writes[DISK_CAPACITY], write_attempts, write_order[DISK_CAPACITY];
static uint32_t write_count;
static bool fail_writes;

/** Return the modeled slot state, copying the event only for valid slots. */
static int fake_read(uint32_t index, app_fault_event_t *event) {
  if (index >= DISK_CAPACITY || event == NULL)
    return -1;
  if (disk_state[index] == 1)
    *event = disk[index];
  return disk_state[index];
}

/** Write an erased slot once, recording order; injected failures and occupied slots fail. */
static bool fake_write(uint32_t index, const app_fault_event_t *event) {
  write_attempts++;
  if (index >= DISK_CAPACITY || fail_writes || disk_state[index] != 0 || event == NULL) {
    return false;
  }
  disk[index] = *event;
  disk_state[index] = 1;
  successful_writes[index]++;
  write_order[write_count++] = index;
  return true;
}

/** Place a valid event for slot index from an earlier boot. */
static void seed_disk(uint32_t index, uint32_t session_id) {
  disk[index] = (app_fault_event_t){.sequence = index + 1U,
                                    .session_id = session_id,
                                    .timestamp_ms = 100U + index,
                                    .source = 0U,
                                    .state = APP_FAULT_STATE_ACTIVE,
                                    .detail = 0x10U + index};
  disk_state[index] = 1;
}

/* A reboot recovers existing slots and appends in a distinct session without editing them. */
static bool recover_and_append(void) {
  app_fault_event_t first = {0}, event = {0};
  seed_disk(0U, 40U);
  seed_disk(1U, 41U);
  first = disk[0];
  app_fault_history_init(1U);
  app_fault_history_attach(fake_read, fake_write, DISK_CAPACITY);
  CHECK(app_fault_history_durable());
  CHECK(app_fault_history_count() == 2U && app_fault_history_pending() == 0U);
  CHECK(write_attempts == 0U);
  CHECK(app_fault_history_get(0U, &event) && same_event(&first, &event));

  app_fault_history_observe(3U, APP_FAULT_STATE_UNKNOWN, 88U, 7U);
  CHECK(app_fault_history_count() == 3U && app_fault_history_pending() == 0U);
  CHECK(write_count == 1U && write_order[0] == 2U && successful_writes[2] == 1U);
  CHECK(app_fault_history_get(2U, &event));
  CHECK(event.sequence == 3U && event.session_id == 42U && event.timestamp_ms == 7U);
  CHECK(event.source == 3U && event.state == APP_FAULT_STATE_UNKNOWN && event.detail == 88U);
  event.detail = 0U;
  CHECK(app_fault_history_get(2U, &event) && event.detail == 88U);
  app_fault_history_flush();
  app_fault_history_observe(3U, APP_FAULT_STATE_UNKNOWN, 88U, 8U);
  CHECK(write_count == 1U && successful_writes[2] == 1U);
  CHECK(app_fault_history_get(0U, &event) && same_event(&first, &event));
  CHECK(same_event(&first, &disk[0]));
  CHECK(successful_writes[0] == 0U && successful_writes[1] == 0U);
  return true;
}

/* Failed storage writes retain all pending copies; retry commits them strictly in order. */
static bool retry_pending_in_order(void) {
  app_fault_event_t first = {0}, second = {0}, event = {0};
  app_fault_history_init(90U);
  app_fault_history_attach(fake_read, fake_write, DISK_CAPACITY);
  CHECK(app_fault_history_durable());
  fail_writes = true;
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 4U, 10U);
  CHECK(!app_fault_history_durable());
  CHECK(app_fault_history_count() == 1U && app_fault_history_pending() == 1U);
  CHECK(app_fault_history_get(0U, &first));
  app_fault_history_observe(0U, APP_FAULT_STATE_HEALTHY, 0U, 20U);
  CHECK(app_fault_history_count() == 2U && app_fault_history_pending() == 2U);
  CHECK(app_fault_history_get(1U, &second));
  CHECK(first.sequence == 1U && second.sequence == 2U);
  CHECK(first.timestamp_ms == 10U && second.timestamp_ms == 20U);
  CHECK(first.session_id == 90U && second.session_id == 90U);
  CHECK(write_count == 0U && disk_state[0] == 0 && disk_state[1] == 0);
  app_fault_history_flush();
  CHECK(app_fault_history_pending() == 2U && app_fault_history_dropped() == 0U);
  CHECK(app_fault_history_get(0U, &event) && same_event(&first, &event));

  fail_writes = false;
  app_fault_history_flush();
  CHECK(app_fault_history_durable());
  CHECK(app_fault_history_pending() == 0U && app_fault_history_count() == 2U);
  CHECK(write_count == 2U && write_order[0] == 0U && write_order[1] == 1U);
  CHECK(successful_writes[0] == 1U && successful_writes[1] == 1U);
  CHECK(app_fault_history_get(0U, &event) && same_event(&first, &event));
  CHECK(app_fault_history_get(1U, &event) && same_event(&second, &event));
  app_fault_history_flush();
  CHECK(write_count == 2U);
  return true;
}

/*
 * A checksum-valid final record this firmware cannot interpret (wrong sequence, source or
 * state) blocks writes; its slot and everything before it stay as found, new events live in
 * RAM and the unreadable record keeps its place in the count.
 */
static bool corruption_blocks_overwrite(unsigned kind) {
  app_fault_event_t old = {0}, event = {0};
  seed_disk(0U, 10U);
  seed_disk(1U, 10U);
  if (kind == 1U)
    disk[1].sequence = 99U;
  else if (kind == 2U)
    disk[1].source = APP_FAULT_SOURCE_COUNT;
  else
    disk[1].state = 3U;
  old = disk[1];
  app_fault_history_init(1U);
  app_fault_history_attach(fake_read, fake_write, DISK_CAPACITY);
  CHECK(!app_fault_history_durable());
  CHECK(app_fault_history_journal_state() == APP_FAULT_JOURNAL_BLOCKED);
  CHECK(app_fault_history_count() == 2U);
  CHECK(!app_fault_history_get(1U, &event));
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 8U, 10U);
  app_fault_history_observe(0U, APP_FAULT_STATE_HEALTHY, 0U, 20U);
  app_fault_history_flush();
  CHECK(app_fault_history_pending() == 2U && app_fault_history_count() == 4U);
  CHECK(app_fault_history_dropped() == 0U && write_attempts == 0U);
  CHECK(app_fault_history_get(2U, &event));
  CHECK(event.sequence == 3U && event.timestamp_ms == 10U && event.detail == 8U);
  CHECK(app_fault_history_get(3U, &event));
  CHECK(event.sequence == 4U && event.state == APP_FAULT_STATE_HEALTHY);
  CHECK(same_event(&old, &disk[1]));
  CHECK(app_fault_history_get(0U, &event) && same_event(&disk[0], &event));
  return true;
}

/* A torn final write (unreadable last slot after a valid one) is skipped, never rewritten. */
static bool torn_tail_is_skipped(bool first_slot) {
  app_fault_event_t event = {0};
  uint32_t torn = first_slot ? 0U : 2U;
  if (!first_slot) {
    seed_disk(0U, 10U);
    seed_disk(1U, 11U);
  }
  disk_state[torn] = -1;
  app_fault_history_init(3U);
  app_fault_history_attach(fake_read, fake_write, DISK_CAPACITY);
  CHECK(app_fault_history_durable());
  CHECK(app_fault_history_count() == torn + 1U);
  CHECK(!app_fault_history_get(torn, &event));
  app_fault_history_observe(4U, APP_FAULT_STATE_ACTIVE, 5U, 50U);
  CHECK(write_count == 1U && write_order[0] == torn + 1U && disk_state[torn] == -1);
  CHECK(app_fault_history_get(torn + 1U, &event));
  CHECK(event.sequence == torn + 2U && event.source == 4U && event.detail == 5U);
  /* Boot IDs continue after the last readable record; the hint wins when it is larger. */
  CHECK(event.session_id == (first_slot ? 3U : 12U));
  return true;
}

/* Several unreadable final slots are not a torn write: treat as foreign data and block. */
static bool foreign_data_blocks(void) {
  for (uint32_t i = 0U; i < 4U; i++)
    disk_state[i] = -1;
  app_fault_history_init(1U);
  app_fault_history_attach(fake_read, fake_write, DISK_CAPACITY);
  CHECK(app_fault_history_journal_state() == APP_FAULT_JOURNAL_BLOCKED);
  CHECK(app_fault_history_count() == 4U);
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 1U, 1U);
  CHECK(app_fault_history_pending() == 1U && write_attempts == 0U);
  return true;
}

/* An I/O error while locating the end attaches nothing; a later attempt can still attach. */
static int failing_read(uint32_t index, app_fault_event_t *event) {
  (void)index;
  (void)event;
  return APP_FAULT_SLOT_IO_ERROR;
}
/** Attach once through a failing reader, then again through a working one. */
static bool attach_io_error_is_retryable(void) {
  seed_disk(0U, 10U);
  app_fault_history_init(1U);
  app_fault_history_attach(failing_read, fake_write, DISK_CAPACITY);
  CHECK(app_fault_history_journal_state() == APP_FAULT_JOURNAL_NO_STORAGE);
  CHECK(app_fault_history_count() == 0U && write_attempts == 0U);
  app_fault_history_attach(fake_read, fake_write, DISK_CAPACITY);
  CHECK(app_fault_history_journal_state() == APP_FAULT_JOURNAL_SAVED);
  CHECK(app_fault_history_count() == 1U && app_fault_history_session() == 11U);
  return true;
}

/*
 * Locating the end of a large journal costs O(log capacity) reads. The reader models a
 * journal of `end` valid slots followed by erased slots and counts every read.
 */
#define SEARCH_CAPACITY (1U << 20U)
static uint32_t search_end, search_reads;
/** Model search_end valid slots followed by erased ones, counting reads. */
static int search_read(uint32_t index, app_fault_event_t *event) {
  search_reads++;
  if (index >= SEARCH_CAPACITY)
    return APP_FAULT_SLOT_IO_ERROR;
  if (index >= search_end)
    return APP_FAULT_SLOT_ERASED;
  *event = (app_fault_event_t){.sequence = index + 1U,
                               .session_id = 7U,
                               .timestamp_ms = index,
                               .source = 1U,
                               .state = APP_FAULT_STATE_ACTIVE,
                               .detail = index};
  return APP_FAULT_SLOT_VALID;
}
/** Accept only the next sequential append. */
static bool search_write(uint32_t index, const app_fault_event_t *event) {
  if (index != search_end || event == NULL || event->sequence != index + 1U)
    return false;
  search_end++;
  return true;
}
/** Attach a journal of end events and check the read count, end position and next append. */
static bool binary_search_attach(uint32_t end) {
  app_fault_event_t event = {0};
  search_end = end;
  app_fault_history_init(1U);
  app_fault_history_attach(search_read, search_write, SEARCH_CAPACITY);
  CHECK(app_fault_history_count() == end);
  CHECK(search_reads <= 21U + 2U);
  CHECK(app_fault_history_journal_state() ==
        (end == SEARCH_CAPACITY ? APP_FAULT_JOURNAL_FULL : APP_FAULT_JOURNAL_SAVED));
  CHECK(app_fault_history_session() == (end == 0U ? 1U : 8U));
  if (end > 0U) {
    CHECK(app_fault_history_get(end - 1U, &event) && event.sequence == end);
  }
  app_fault_history_observe(2U, APP_FAULT_STATE_ACTIVE, 9U, 1U);
  CHECK(app_fault_history_count() == end + 1U);
  CHECK(app_fault_history_get(end, &event) && event.sequence == end + 1U);
  if (end < SEARCH_CAPACITY) {
    CHECK(search_end == end + 1U && app_fault_history_pending() == 0U);
  } else {
    CHECK(search_end == end && app_fault_history_pending() == 1U);
  }
  return true;
}

/* Journal state reports why events are or are not being saved, in priority order. */
static bool journal_states(void) {
  CHECK(app_fault_history_journal_state() == APP_FAULT_JOURNAL_NO_STORAGE);
  app_fault_history_init(1U);
  app_fault_history_attach(fake_read, fake_write, DISK_CAPACITY);
  CHECK(app_fault_history_journal_state() == APP_FAULT_JOURNAL_SAVED);
  fail_writes = true;
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 1U, 1U);
  CHECK(app_fault_history_journal_state() == APP_FAULT_JOURNAL_WRITE_FAILED);
  fail_writes = false;
  app_fault_history_flush();
  CHECK(app_fault_history_journal_state() == APP_FAULT_JOURNAL_SAVED);
  for (uint32_t i = 1U; i < DISK_CAPACITY; i++)
    app_fault_history_observe(
        0U, (i % 2U) ? APP_FAULT_STATE_HEALTHY : APP_FAULT_STATE_ACTIVE, 1U, i);
  CHECK(app_fault_history_journal_state() == APP_FAULT_JOURNAL_FULL);
  CHECK(app_fault_history_count() == DISK_CAPACITY && app_fault_history_pending() == 0U);
  return true;
}

/*
 * Capacity exhaustion cannot write outside the journal or reuse an occupied slot. This
 * boot's new events are still listed from RAM (marked not saved) instead of being lost.
 */
static bool full_backend_never_overwrites(void) {
  app_fault_event_t retained[DISK_CAPACITY], event = {0};
  for (uint32_t i = 0U; i < DISK_CAPACITY; i++) {
    seed_disk(i, 12U);
    retained[i] = disk[i];
  }
  app_fault_history_init(1U);
  app_fault_history_attach(fake_read, fake_write, DISK_CAPACITY);
  CHECK(!app_fault_history_durable());
  CHECK(app_fault_history_journal_state() == APP_FAULT_JOURNAL_FULL);
  CHECK(app_fault_history_count() == DISK_CAPACITY && app_fault_history_pending() == 0U);
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 1U, 10U);
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 1U, 11U);
  app_fault_history_observe(0U, APP_FAULT_STATE_HEALTHY, 0U, 20U);
  app_fault_history_flush();
  CHECK(app_fault_history_dropped() == 0U);
  CHECK(app_fault_history_count() == DISK_CAPACITY + 2U && app_fault_history_pending() == 2U);
  CHECK(app_fault_history_get(DISK_CAPACITY, &event) && event.sequence == DISK_CAPACITY + 1U);
  CHECK(event.timestamp_ms == 10U && event.session_id == 13U);
  CHECK(write_attempts == 0U);
  for (uint32_t i = 0U; i < DISK_CAPACITY; i++) {
    CHECK(app_fault_history_get(i, &event) && same_event(&retained[i], &event));
    CHECK(same_event(&retained[i], &disk[i]));
  }
  return true;
}

/* Invalid attachment arguments remain retryable; a live journal cannot be replaced. */
static bool attach_guards(void) {
  app_fault_history_init(1U);
  app_fault_history_attach(NULL, fake_write, DISK_CAPACITY);
  app_fault_history_attach(fake_read, NULL, DISK_CAPACITY);
  app_fault_history_attach(fake_read, fake_write, 0U);
  CHECK(!app_fault_history_durable() && app_fault_history_count() == 0U);
  app_fault_history_attach(fake_read, fake_write, DISK_CAPACITY);
  CHECK(app_fault_history_durable());
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 1U, 1U);
  app_fault_history_attach(fake_read, fake_write, 1U);
  app_fault_history_observe(0U, APP_FAULT_STATE_HEALTHY, 0U, 2U);
  CHECK(app_fault_history_count() == 2U && write_count == 2U);
  CHECK(app_fault_history_dropped() == 0U && app_fault_history_durable());
  return true;
}

/* Exhausting the session namespace must not silently label new writes as session zero. */
static bool exhausted_session_blocks_writes(void) {
  app_fault_event_t retained = {0};
  seed_disk(0U, UINT32_MAX);
  retained = disk[0];
  app_fault_history_init(1U);
  app_fault_history_attach(fake_read, fake_write, DISK_CAPACITY);
  CHECK(app_fault_history_count() == 1U);
  CHECK(!app_fault_history_durable());
  app_fault_history_observe(0U, APP_FAULT_STATE_ACTIVE, 8U, 10U);
  app_fault_history_flush();
  CHECK(write_attempts == 0U);
  CHECK(same_event(&retained, &disk[0]));
  return true;
}

/** Run the case named by argv[1] (with an optional numeric argument) in this fresh process. */
int main(int argc, char **argv) {
  if (argc != 2 && argc != 3)
    return 2;
  bool passed;
  if (strcmp(argv[1], "lifecycle") == 0)
    passed = lifecycle();
  else if (strcmp(argv[1], "guards") == 0)
    passed = guards_and_source_isolation();
  else if (strcmp(argv[1], "overflow") == 0)
    passed = overflow_never_overwrites();
  else if (strcmp(argv[1], "recover") == 0)
    passed = recover_and_append();
  else if (strcmp(argv[1], "retry") == 0)
    passed = retry_pending_in_order();
  else if (strcmp(argv[1], "torn-tail") == 0)
    passed = torn_tail_is_skipped(false);
  else if (strcmp(argv[1], "torn-first") == 0)
    passed = torn_tail_is_skipped(true);
  else if (strcmp(argv[1], "foreign") == 0)
    passed = foreign_data_blocks();
  else if (strcmp(argv[1], "attach-io-error") == 0)
    passed = attach_io_error_is_retryable();
  else if (strcmp(argv[1], "journal-states") == 0)
    passed = journal_states();
  else if (strcmp(argv[1], "search") == 0 && argc == 3)
    passed = binary_search_attach((uint32_t)strtoul(argv[2], NULL, 10));
  else if (strcmp(argv[1], "corrupt-sequence") == 0)
    passed = corruption_blocks_overwrite(1U);
  else if (strcmp(argv[1], "corrupt-source") == 0)
    passed = corruption_blocks_overwrite(2U);
  else if (strcmp(argv[1], "corrupt-state") == 0)
    passed = corruption_blocks_overwrite(3U);
  else if (strcmp(argv[1], "disk-full") == 0)
    passed = full_backend_never_overwrites();
  else if (strcmp(argv[1], "attach-guards") == 0)
    passed = attach_guards();
  else if (strcmp(argv[1], "session-full") == 0)
    passed = exhausted_session_blocks_writes();
  else
    return 2;
  return passed ? 0 : 1;
}
