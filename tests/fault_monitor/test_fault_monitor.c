/* Drive the production episode monitor through long synthetic timelines, one case per process. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_fault_history.h"
#include "app_fault_monitor.h"

#define CHECK(condition)                                                                           \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #condition);                         \
      dump_history();                                                                              \
      return false;                                                                                \
    }                                                                                              \
  } while (0)

/* Always-working RAM journal so persistence problems do not appear as extra events. */
#define JOURNAL_SLOTS 4096U
static app_fault_event_t journal[JOURNAL_SLOTS];
static bool journal_used[JOURNAL_SLOTS];
static app_can_dashboard_data_t can;
static app_aux_ui_state_t aux;
static app_emmc_log_status_t storage = {.ready = true};

/** Return the stored slot, or erased for slots never written. */
static int journal_read(uint32_t index, app_fault_event_t *event) {
  if (index >= JOURNAL_SLOTS)
    return APP_FAULT_SLOT_IO_ERROR;
  if (!journal_used[index])
    return APP_FAULT_SLOT_ERASED;
  *event = journal[index];
  return APP_FAULT_SLOT_VALID;
}
/** Write an erased slot once. */
static bool journal_write(uint32_t index, const app_fault_event_t *event) {
  if (index >= JOURNAL_SLOTS || journal_used[index])
    return false;
  journal[index] = *event;
  journal_used[index] = true;
  return true;
}
/** Print every recorded event to stderr to explain a failed expectation. */
static void dump_history(void) {
  char text[96];
  for (uint32_t i = 0U; i < app_fault_history_count(); i++) {
    app_fault_event_t event;
    if (!app_fault_history_get(i, &event))
      continue;
    app_fault_describe(&event, text, sizeof(text));
    fprintf(stderr,
            "  #%lu t=%lu src=%u state=%u detail=%lu %s\n",
            (unsigned long)event.sequence,
            (unsigned long)event.timestamp_ms,
            (unsigned)event.source,
            (unsigned)event.state,
            (unsigned long)event.detail,
            text);
  }
}
/** Attach the RAM journal like the firmware attaches the eMMC journal at boot. */
static void boot(void) {
  app_fault_history_init(1U);
  app_fault_history_attach(journal_read, journal_write, JOURNAL_SLOTS);
}
/** Mark every BMS signal as just received at now. */
static void bms_replies(uint32_t now) {
  can.valid_mask = (uint16_t)((1U << APP_CAN_SIGNAL_COUNT) - 1U);
  for (unsigned i = 0U; i < APP_CAN_SIGNAL_COUNT; i++)
    can.signal_tick_ms[i] = now;
}
/** Run one GUI refresh. */
static void refresh(uint32_t now) {
  app_fault_monitor_update(&can, &aux, &storage, now);
}
/** Copy event index into event; false when absent. */
static bool event_at(uint32_t index, app_fault_event_t *event) {
  return app_fault_history_get(index, event);
}

/* BMS never answers for 15 minutes while every request times out: one entry in total. */
static bool silent_bms(void) {
  boot();
  for (uint32_t now = 0U; now <= 15U * 60000U; now += 100U) {
    can.request_timeouts = now / 110U;
    refresh(now);
  }
  app_fault_event_t event;
  CHECK(app_fault_history_count() == 1U);
  CHECK(event_at(0U, &event) && event.source == APP_FAULT_BMS_LINK);
  CHECK(event.state == APP_FAULT_STATE_ACTIVE && event.detail == APP_FAULT_LINK_NEVER_CONNECTED);
  CHECK(event.timestamp_ms == APP_FAULT_STARTUP_GRACE_MS);
  return true;
}

/* A BMS that starts replying inside the startup grace leaves no entry at all. */
static bool late_bms(void) {
  boot();
  for (uint32_t now = 0U; now <= 60000U; now += 100U) {
    if (now >= 8000U)
      bms_replies(now);
    else
      can.request_timeouts = now / 110U;
    refresh(now);
  }
  CHECK(app_fault_history_count() == 0U);
  return true;
}

/* A 30 s outage is one LOST entry and one CLEAR after the hold, nothing per signal or timeout. */
static bool bms_outage(void) {
  boot();
  uint32_t timeouts = 0U;
  for (uint32_t now = 0U; now <= 80000U; now += 100U) {
    if (now <= 20000U || now >= 50000U)
      bms_replies(now);
    else
      timeouts++;
    can.request_timeouts = timeouts;
    refresh(now);
  }
  app_fault_event_t lost, cleared;
  CHECK(app_fault_history_count() == 2U);
  CHECK(event_at(0U, &lost) && lost.source == APP_FAULT_BMS_LINK);
  CHECK(lost.state == APP_FAULT_STATE_ACTIVE && lost.detail == APP_FAULT_LINK_LOST);
  CHECK(lost.timestamp_ms > 22000U && lost.timestamp_ms <= 22200U);
  CHECK(event_at(1U, &cleared) && cleared.source == APP_FAULT_BMS_LINK);
  CHECK(cleared.state == APP_FAULT_STATE_HEALTHY);
  CHECK(cleared.timestamp_ms == 50000U + APP_FAULT_CLEAR_HOLD_MS);
  return true;
}

/* One PID unanswered: the missing signal once, the timeout episode once, each cleared once. */
static bool pid_missing(void) {
  boot();
  uint32_t timeouts = 0U;
  for (uint32_t now = 0U; now <= 100000U; now += 100U) {
    bms_replies(now);
    if (now < 30000U) {
      can.valid_mask &= (uint16_t) ~(1U << APP_CAN_SIGNAL_TEMP_LOW);
      if (now % 600U == 0U && now > 0U)
        timeouts++;
    }
    can.request_timeouts = timeouts;
    refresh(now);
  }
  app_fault_event_t event;
  CHECK(app_fault_history_count() == 4U);
  CHECK(event_at(0U, &event) && event.source == APP_CAN_SIGNAL_TEMP_LOW);
  CHECK(event.state == APP_FAULT_STATE_UNKNOWN && event.detail == APP_FAULT_SIGNAL_NEVER_RECEIVED);
  CHECK(event.timestamp_ms == APP_CAN_STALE_MS);
  CHECK(event_at(1U, &event) && event.source == APP_FAULT_CAN_TIMEOUT);
  CHECK(event.state == APP_FAULT_STATE_ACTIVE && event.timestamp_ms == 3600U);
  CHECK(event_at(2U, &event) && event.source == APP_CAN_SIGNAL_TEMP_LOW);
  CHECK(event.state == APP_FAULT_STATE_HEALTHY && event.timestamp_ms == 35000U);
  CHECK(event_at(3U, &event) && event.source == APP_FAULT_CAN_TIMEOUT);
  CHECK(event.state == APP_FAULT_STATE_HEALTHY && event.detail == timeouts);
  CHECK(event.timestamp_ms == 29400U + APP_FAULT_ERROR_QUIET_MS);
  return true;
}

/*
 * Timeouts during an outage belong to the BMS link entry: a timeout episode that was already
 * open keeps counting only until the link is declared down, and its CLEAR reports that count.
 */
static bool timeouts_then_outage(void) {
  boot();
  uint32_t timeouts = 0U, counted = 0U;
  for (uint32_t now = 0U; now <= 120000U; now += 100U) {
    bool silent = now > 20000U && now < 50000U;
    if (!silent)
      bms_replies(now);
    if ((now > 10000U && now <= 20000U && now % 600U == 0U) || silent) {
      timeouts++;
      counted += now <= 22000U ? 1U : 0U; /* link is declared down at the 22.1 s refresh */
    }
    can.request_timeouts = timeouts;
    refresh(now);
  }
  app_fault_event_t event;
  uint32_t found = 0U;
  for (uint32_t i = 0U; i < app_fault_history_count(); i++) {
    CHECK(event_at(i, &event));
    if (event.source != APP_FAULT_CAN_TIMEOUT)
      continue;
    found++;
    if (event.state == APP_FAULT_STATE_HEALTHY) {
      CHECK(event.detail == counted && counted < timeouts);
      CHECK(event.timestamp_ms == 22000U + APP_FAULT_ERROR_QUIET_MS);
    }
  }
  CHECK(found == 2U);
  return true;
}

/* A signal that stops while its siblings keep updating is reported once as stopped. */
static bool signal_stops(void) {
  boot();
  for (uint32_t now = 0U; now <= 40000U; now += 100U) {
    uint32_t held = can.signal_tick_ms[APP_CAN_SIGNAL_CELL_LOW];
    bms_replies(now);
    if (now > 10000U && now < 20000U)
      can.signal_tick_ms[APP_CAN_SIGNAL_CELL_LOW] = held;
    refresh(now);
  }
  app_fault_event_t event;
  CHECK(app_fault_history_count() == 2U);
  CHECK(event_at(0U, &event) && event.source == APP_CAN_SIGNAL_CELL_LOW);
  CHECK(event.state == APP_FAULT_STATE_UNKNOWN && event.detail == APP_FAULT_SIGNAL_STOPPED);
  CHECK(event.timestamp_ms == 12100U);
  CHECK(event_at(1U, &event) && event.state == APP_FAULT_STATE_HEALTHY);
  CHECK(event.timestamp_ms == 25000U);
  return true;
}

/* A wired IMD toggling every second for a minute is one FAULT and one CLEAR. */
static bool flapping_status(void) {
  boot();
  aux.status_signals[APP_STATUS_SIGNAL_IMD].wired = true;
  aux.status_signals[APP_STATUS_SIGNAL_IMD].valid = true;
  for (uint32_t now = 0U; now <= 90000U; now += 100U) {
    bms_replies(now);
    aux.status_signals[APP_STATUS_SIGNAL_IMD].fault_active =
        now <= 60000U && (now / 1000U) % 2U == 0U;
    refresh(now);
  }
  app_fault_event_t event;
  CHECK(app_fault_history_count() == 2U);
  CHECK(event_at(0U, &event) && event.source == APP_FAULT_AUX_STATUS_FIRST);
  CHECK(event.state == APP_FAULT_STATE_ACTIVE && event.timestamp_ms == 0U);
  CHECK(event_at(1U, &event) && event.state == APP_FAULT_STATE_HEALTHY);
  /* Last fault sample is at 60.0 s; healthy from the 60.1 s refresh, cleared one hold later. */
  CHECK(event.timestamp_ms == 60100U + APP_FAULT_CLEAR_HOLD_MS);
  return true;
}

/* Inputs without a provider never reach the history; wired-but-silent ones do, once. */
static bool unwired_and_wired_inputs(void) {
  boot();
  for (uint32_t now = 0U; now <= 60000U; now += 100U) {
    bms_replies(now);
    refresh(now);
  }
  CHECK(app_fault_history_count() == 0U);
  for (unsigned i = 0U; i < APP_STATUS_SIGNAL_COUNT; i++)
    aux.status_signals[i].wired = true;
  aux.speed_wired = aux.drive_mode_wired = true;
  for (uint32_t now = 60000U; now <= 120000U; now += 100U) {
    bms_replies(now);
    refresh(now);
  }
  CHECK(app_fault_history_count() == APP_STATUS_SIGNAL_COUNT + 2U);
  for (uint32_t i = 0U; i < app_fault_history_count(); i++) {
    app_fault_event_t event;
    CHECK(event_at(i, &event) && event.state == APP_FAULT_STATE_UNKNOWN);
    CHECK(event.timestamp_ms == 60000U);
  }
  return true;
}

/* Rejected frames for two minutes: one FAULT at the first, one CLEAR after the quiet time. */
static bool rejected_frames(void) {
  boot();
  for (uint32_t now = 0U; now <= 240000U; now += 100U) {
    bms_replies(now);
    if (now > 0U && now <= 120000U && now % 200U == 0U)
      can.rejected_frames++;
    refresh(now);
  }
  app_fault_event_t event;
  CHECK(app_fault_history_count() == 2U);
  CHECK(event_at(0U, &event) && event.source == APP_FAULT_CAN_REJECTED);
  CHECK(event.state == APP_FAULT_STATE_ACTIVE && event.detail == 1U && event.timestamp_ms == 200U);
  CHECK(event_at(1U, &event) && event.state == APP_FAULT_STATE_HEALTHY);
  CHECK(event.detail == 600U && event.timestamp_ms == 120000U + APP_FAULT_ERROR_QUIET_MS);
  return true;
}

/* A transient write error, a card outage and telemetry overflow are separate episodes. */
static bool storage_episodes(void) {
  boot();
  for (uint32_t now = 0U; now <= 200000U; now += 100U) {
    bms_replies(now);
    if (now == 5000U)
      storage.io_errors++;
    storage.ready = !(now >= 100000U && now < 110000U);
    storage.overflow = now >= 150000U;
    refresh(now);
  }
  app_fault_event_t event;
  CHECK(app_fault_history_count() == 5U);
  CHECK(event_at(0U, &event) && event.source == APP_FAULT_STORAGE);
  CHECK(event.detail == APP_FAULT_STORAGE_WRITE_ERRORS && event.timestamp_ms == 5000U);
  CHECK(event_at(1U, &event) && event.state == APP_FAULT_STATE_HEALTHY && event.detail == 1U);
  CHECK(event.timestamp_ms == 65000U);
  CHECK(event_at(2U, &event) && event.detail == APP_FAULT_STORAGE_NOT_READY);
  CHECK(event.timestamp_ms == 100000U);
  CHECK(event_at(3U, &event) && event.source == APP_FAULT_STORAGE_FULL);
  CHECK(event.state == APP_FAULT_STATE_ACTIVE && event.timestamp_ms == 150000U);
  CHECK(event_at(4U, &event) && event.source == APP_FAULT_STORAGE);
  CHECK(event.state == APP_FAULT_STATE_HEALTHY && event.timestamp_ms == 109900U + 60000U);
  return true;
}

/* Configured limits record one episode with the value; none are configured by default. */
static bool limits(bool configured) {
  app_fault_limit_t table[APP_CAN_SIGNAL_COUNT] = {0};
  table[APP_CAN_SIGNAL_CELL_HIGH] = (app_fault_limit_t){true, 25000, 42000};
  boot();
  if (configured)
    app_fault_monitor_set_limits(table);
  for (uint32_t now = 0U; now <= 40000U; now += 100U) {
    bms_replies(now);
    can.highest_cell_voltage_100uv = 41000U;
    if (now >= 10000U && now < 20000U)
      can.highest_cell_voltage_100uv = (now / 1000U) % 2U == 0U ? 42500U : 41900U;
    refresh(now);
  }
  if (!configured) {
    CHECK(app_fault_history_count() == 0U);
    return true;
  }
  app_fault_event_t event;
  char text[96];
  CHECK(app_fault_history_count() == 2U);
  CHECK(event_at(0U, &event) && event.source == APP_CAN_SIGNAL_CELL_HIGH);
  CHECK(event.state == APP_FAULT_STATE_ACTIVE && event.timestamp_ms == 10000U);
  CHECK(event.detail == ((APP_FAULT_LIMIT_ABOVE << 16U) | 42500U));
  app_fault_describe(&event, text, sizeof(text));
  CHECK(strcmp(text, "High cell voltage: above limit (4250 mV)") == 0);
  CHECK(event_at(1U, &event) && event.state == APP_FAULT_STATE_HEALTHY);
  /* 19 s is a within-limit second; from then on the value stays at 4.1 V. */
  CHECK(event.timestamp_ms == 19000U + APP_FAULT_CLEAR_HOLD_MS);
  return true;
}

/* Without a card the journal itself is reported once, in RAM. */
static bool no_journal(void) {
  app_fault_history_init(1U);
  for (uint32_t now = 0U; now <= 20000U; now += 100U) {
    bms_replies(now);
    refresh(now);
  }
  app_fault_event_t event;
  CHECK(app_fault_history_count() == 1U && app_fault_history_pending() == 1U);
  CHECK(event_at(0U, &event) && event.source == APP_FAULT_HISTORY_STORAGE);
  CHECK(event.detail == APP_FAULT_JOURNAL_NO_STORAGE && event.timestamp_ms == 0U);
  return true;
}

/* Every event kind reads as words on the history screen and in exports. */
static bool descriptions(void) {
  static const struct {
    app_fault_event_t event;
    const char *text;
  } cases[] = {
      {{1U, 1U, 0U, APP_FAULT_BMS_LINK, APP_FAULT_STATE_ACTIVE, APP_FAULT_LINK_NEVER_CONNECTED},
       "BMS link: no reply since boot"},
      {{1U, 1U, 0U, APP_FAULT_BMS_LINK, APP_FAULT_STATE_ACTIVE, APP_FAULT_LINK_LOST},
       "BMS link: replies stopped"},
      {{1U, 1U, 0U, APP_FAULT_BMS_LINK, APP_FAULT_STATE_HEALTHY, 0U}, "BMS link: replies OK again"},
      {{1U, 1U, 0U, 0U, APP_FAULT_STATE_UNKNOWN, APP_FAULT_SIGNAL_STOPPED},
       "Pack voltage: stopped updating"},
      {{1U, 1U, 0U, 3U, APP_FAULT_STATE_UNKNOWN, APP_FAULT_SIGNAL_NEVER_RECEIVED},
       "State of charge: never received"},
      {{1U, 1U, 0U, 2U, APP_FAULT_STATE_ACTIVE, (APP_FAULT_LIMIT_BELOW << 16U) | 0xFC18U},
       "Pack current: below limit (-100.0 A)"},
      {{1U, 1U, 0U, 4U, APP_FAULT_STATE_ACTIVE, (APP_FAULT_LIMIT_ABOVE << 16U) | 0x3CU},
       "High temperature: above limit (60 C)"},
      {{1U, 1U, 0U, 4U, APP_FAULT_STATE_HEALTHY, 0U}, "High temperature: OK again"},
      {{1U, 1U, 0U, APP_FAULT_CAN_TIMEOUT, APP_FAULT_STATE_ACTIVE, 7U},
       "CAN request timeout: started (total 7)"},
      {{1U, 1U, 0U, APP_FAULT_CAN_TIMEOUT, APP_FAULT_STATE_HEALTHY, 3U},
       "CAN request timeout: stopped, 3 in episode"},
      {{1U, 1U, 0U, APP_FAULT_STORAGE, APP_FAULT_STATE_ACTIVE, APP_FAULT_STORAGE_NOT_READY},
       "Storage: card not ready"},
      {{1U, 1U, 0U, APP_FAULT_STORAGE, APP_FAULT_STATE_HEALTHY, 2U},
       "Storage: OK again, 2 errors in episode"},
      {{1U, 1U, 0U, APP_FAULT_STORAGE_FULL, APP_FAULT_STATE_ACTIVE, 0U}, "Telemetry storage: full"},
      {{1U, 1U, 0U, APP_FAULT_HISTORY_STORAGE, APP_FAULT_STATE_ACTIVE, APP_FAULT_JOURNAL_FULL},
       "History storage: journal full, not saved"},
      {{1U, 1U, 0U, 10U, APP_FAULT_STATE_ACTIVE, 0U}, "IMD: fault"},
      {{1U, 1U, 0U, 14U, APP_FAULT_STATE_UNKNOWN, 0U}, "BSPD: no signal"},
      {{1U, 1U, 0U, APP_FAULT_SPEED, APP_FAULT_STATE_UNKNOWN, 0U}, "Speed input: no signal"},
      {{1U, 1U, 0U, 30U, APP_FAULT_STATE_ACTIVE, 5U}, "Unknown: state 2 [5]"},
  };
  char text[96];
  for (unsigned i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
    app_fault_describe(&cases[i].event, text, sizeof(text));
    if (strcmp(text, cases[i].text) != 0) {
      fprintf(stderr, "case %u: got '%s' want '%s'\n", i, text, cases[i].text);
      return false;
    }
  }
  app_fault_describe(&cases[0].event, text, 8U);
  CHECK(strlen(text) == 7U);
  app_fault_describe(NULL, text, sizeof(text));
  CHECK(text[0] == '\0');
  return true;
}

/** Run the timeline named by argv[1] in this fresh process. */
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  static const struct {
    const char *name;
    bool (*run)(void);
  } cases[] = {{"silent-bms", silent_bms},
               {"late-bms", late_bms},
               {"bms-outage", bms_outage},
               {"pid-missing", pid_missing},
               {"timeouts-outage", timeouts_then_outage},
               {"signal-stops", signal_stops},
               {"flapping-status", flapping_status},
               {"inputs", unwired_and_wired_inputs},
               {"rejected", rejected_frames},
               {"storage", storage_episodes},
               {"no-journal", no_journal},
               {"descriptions", descriptions}};
  if (strcmp(argv[1], "limits") == 0)
    return limits(true) ? 0 : 1;
  if (strcmp(argv[1], "limits-default") == 0)
    return limits(false) ? 0 : 1;
  for (unsigned i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++)
    if (strcmp(argv[1], cases[i].name) == 0)
      return cases[i].run() ? 0 : 1;
  return 2;
}
