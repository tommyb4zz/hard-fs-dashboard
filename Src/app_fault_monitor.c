/* Episode-based malfunction detection over the live snapshot; history stays append-only. */
#include "app_fault_monitor.h"

#include <stdio.h>
#include <string.h>

/* One open (non-healthy) entry per source; key changes record a new entry in the episode. */
typedef struct {
  bool open;
  uint32_t key;
  bool clearing;
  uint32_t clear_since_ms;
} app_fault_episode_t;

/* Counter value at the previous refresh, increments counted in the episode and their times. */
typedef struct {
  uint32_t last;
  uint32_t start;
  uint32_t counted;
  uint32_t first_increment_ms;
  uint32_t last_increment_ms;
  bool armed;
} app_fault_counter_t;

static app_fault_episode_t episodes[APP_FAULT_SOURCE_COUNT];
static app_fault_counter_t rejected_frames, request_timeouts, storage_errors;
static app_fault_limit_t limits[APP_CAN_SIGNAL_COUNT];
static bool link_up, link_ever_up, startup_over;
static uint32_t link_up_since_ms;

/** Copy caller limits so later edits to the caller's table cannot change detection. */
void app_fault_monitor_set_limits(const app_fault_limit_t table[APP_CAN_SIGNAL_COUNT]) {
  if (table == NULL)
    memset(limits, 0, sizeof(limits));
  else
    memcpy(limits, table, sizeof(limits));
}

/** Record a bad state when it starts or changes kind; repeats of the same kind add nothing. */
static void
episode_bad(uint16_t source, uint8_t state, uint32_t detail, uint32_t key, uint32_t now) {
  app_fault_episode_t *episode = &episodes[source];
  episode->clearing = false;
  key = (key << 8U) | state;
  if (episode->open && episode->key == key)
    return;
  episode->open = true;
  episode->key = key;
  app_fault_history_observe(source, state, detail, now);
}

/** Record CLEAR once the condition has stayed healthy for hold_ms; no entry if none is open. */
static void episode_good(uint16_t source, uint32_t clear_detail, uint32_t hold_ms, uint32_t now) {
  app_fault_episode_t *episode = &episodes[source];
  if (!episode->open)
    return;
  if (!episode->clearing) {
    episode->clearing = true;
    episode->clear_since_ms = now;
  }
  if ((uint32_t)(now - episode->clear_since_ms) < hold_ms)
    return;
  episode->open = false;
  episode->clearing = false;
  app_fault_history_observe(source, APP_FAULT_STATE_HEALTHY, clear_detail, now);
}

/**
 * Open an episode once increments have been seen for confirm_ms while counting, extend it on
 * later increments and close it after APP_FAULT_ERROR_QUIET_MS without any. ACTIVE detail is
 * the running total when confirmed; CLEAR detail is the number of counted increments.
 * Increments while counting is false (timeouts during a link outage, which the BMS link
 * episode already records) are absorbed: not counted, and they cancel an unconfirmed start.
 */
static void counter_update(uint16_t source,
                           app_fault_counter_t *counter,
                           uint32_t value,
                           bool counting,
                           uint32_t confirm_ms,
                           uint32_t now) {
  if (value != counter->last) {
    uint32_t increments = value - counter->last;
    counter->last = value;
    if (counting) {
      if (!episodes[source].open && !counter->armed) {
        counter->armed = true;
        counter->counted = 0U;
        counter->first_increment_ms = now;
      }
      counter->counted += increments;
      counter->last_increment_ms = now;
    }
  }
  if (!counting)
    counter->armed = false;
  if (counter->armed && (uint32_t)(now - counter->first_increment_ms) >= confirm_ms) {
    counter->armed = false;
    episode_bad(source, APP_FAULT_STATE_ACTIVE, counter->last, 0U, now);
  }
  if (episodes[source].open &&
      (uint32_t)(now - counter->last_increment_ms) >= APP_FAULT_ERROR_QUIET_MS)
    episode_good(source, counter->counted, 0U, now);
}

/** Return one received measurement in its native units for limit comparison. */
static int32_t signal_value(const app_can_dashboard_data_t *can, unsigned signal) {
  switch (signal) {
  case APP_CAN_SIGNAL_PACK_VOLTAGE:
    return can->pack_voltage_deci_volts;
  case APP_CAN_SIGNAL_PACK_SUMMED_VOLTAGE:
    return can->pack_summed_voltage_deci_volts;
  case APP_CAN_SIGNAL_PACK_CURRENT:
    return can->pack_current_deci_amps;
  case APP_CAN_SIGNAL_SOC:
    return can->pack_state_of_charge;
  case APP_CAN_SIGNAL_TEMP_HIGH:
    return can->highest_temperature_c;
  case APP_CAN_SIGNAL_TEMP_LOW:
    return can->lowest_temperature_c;
  case APP_CAN_SIGNAL_TEMP_AVG:
    return can->average_temperature_c;
  case APP_CAN_SIGNAL_CELL_LOW:
    return can->lowest_cell_voltage_100uv;
  case APP_CAN_SIGNAL_CELL_HIGH:
    return can->highest_cell_voltage_100uv;
  default:
    return can->average_cell_voltage_100uv;
  }
}

/** Age of a received signal on the GUI clock; a sample stamped after now counts as fresh. */
static uint32_t signal_age(const app_can_dashboard_data_t *can, unsigned signal, uint32_t now) {
  uint32_t age = now - can->signal_tick_ms[signal];
  return age > 0x7FFFFFFFU ? 0U : age;
}

/**
 * BMS link first, then per-signal episodes only while the link is up. A whole-link outage is
 * one entry, not ten. A signal is individually missing when its siblings keep updating but it
 * has not (or never has) for APP_CAN_STALE_MS after the link came up.
 */
static void check_can(const app_can_dashboard_data_t *can, uint32_t now) {
  bool received_any = false;
  uint32_t newest_age = UINT32_MAX;
  for (unsigned i = 0U; i < APP_CAN_SIGNAL_COUNT; ++i) {
    if ((can->valid_mask & (1U << i)) != 0U) {
      uint32_t age = signal_age(can, i, now);
      newest_age = age < newest_age ? age : newest_age;
      received_any = true;
    }
  }
  bool up = received_any && newest_age <= APP_CAN_STALE_MS;
  if (up && !link_up)
    link_up_since_ms = now;
  link_up = up;
  if (up) {
    link_ever_up = true;
    episode_good(APP_FAULT_BMS_LINK, 0U, APP_FAULT_CLEAR_HOLD_MS, now);
  } else if (link_ever_up || startup_over) {
    episode_bad(APP_FAULT_BMS_LINK,
                APP_FAULT_STATE_ACTIVE,
                link_ever_up ? APP_FAULT_LINK_LOST : APP_FAULT_LINK_NEVER_CONNECTED,
                0U,
                now);
  }
  if (!up || (uint32_t)(now - link_up_since_ms) < APP_CAN_STALE_MS)
    return;
  for (unsigned i = 0U; i < APP_CAN_SIGNAL_COUNT; ++i) {
    uint16_t source = (uint16_t)i;
    if ((can->valid_mask & (1U << i)) == 0U) {
      episode_bad(source, APP_FAULT_STATE_UNKNOWN, APP_FAULT_SIGNAL_NEVER_RECEIVED, 0U, now);
      continue;
    }
    uint32_t age = signal_age(can, i, now);
    if (age - newest_age > APP_CAN_STALE_MS) {
      episode_bad(source, APP_FAULT_STATE_UNKNOWN, APP_FAULT_SIGNAL_STOPPED, 0U, now);
      continue;
    }
    if (age > APP_CAN_STALE_MS)
      continue; /* Stale together with its siblings: the link episode owns this. */
    int32_t value = signal_value(can, i);
    uint32_t kind = !limits[i].enabled          ? 0U
                    : value > limits[i].maximum ? APP_FAULT_LIMIT_ABOVE
                    : value < limits[i].minimum ? APP_FAULT_LIMIT_BELOW
                                                : 0U;
    if (kind != 0U)
      episode_bad(
          source, APP_FAULT_STATE_ACTIVE, (kind << 16U) | ((uint32_t)value & 0xFFFFU), kind, now);
    else
      episode_good(source, 0U, APP_FAULT_CLEAR_HOLD_MS, now);
  }
}

/** Wired inputs only: fault is recorded at once, missing data after the startup grace. */
static void check_input(uint16_t source, bool wired, bool valid, bool fault, uint32_t now) {
  if (!wired)
    return;
  if (valid && fault)
    episode_bad(source, APP_FAULT_STATE_ACTIVE, 0U, 0U, now);
  else if (!valid) {
    if (startup_over)
      episode_bad(source, APP_FAULT_STATE_UNKNOWN, 0U, 0U, now);
  } else
    episode_good(source, 0U, APP_FAULT_CLEAR_HOLD_MS, now);
}

/** Card availability, write failures (state or counted between refreshes) and telemetry space. */
static void check_storage(const app_emmc_log_status_t *storage, uint32_t now) {
  bool failing = !storage->ready || storage->io_failed;
  uint32_t reason = storage->ready ? APP_FAULT_STORAGE_WRITE_ERRORS : APP_FAULT_STORAGE_NOT_READY;
  if (storage->io_errors != storage_errors.last || failing) {
    if (!episodes[APP_FAULT_STORAGE].open)
      storage_errors.start = storage_errors.last;
    storage_errors.last = storage->io_errors;
    storage_errors.last_increment_ms = now;
    episode_bad(APP_FAULT_STORAGE, APP_FAULT_STATE_ACTIVE, reason, 0U, now);
  } else if ((uint32_t)(now - storage_errors.last_increment_ms) >= APP_FAULT_ERROR_QUIET_MS) {
    episode_good(APP_FAULT_STORAGE, storage_errors.last - storage_errors.start, 0U, now);
  }
  if (storage->overflow)
    episode_bad(APP_FAULT_STORAGE_FULL, APP_FAULT_STATE_ACTIVE, 0U, 0U, now);
  else
    episode_good(APP_FAULT_STORAGE_FULL, 0U, APP_FAULT_CLEAR_HOLD_MS, now);
}

/** Evaluate every source for one GUI refresh. */
void app_fault_monitor_update(const app_can_dashboard_data_t *can,
                              const app_aux_ui_state_t *aux,
                              const app_emmc_log_status_t *storage,
                              uint32_t now_ms) {
  if (can == NULL || aux == NULL || storage == NULL)
    return;
  startup_over = startup_over || now_ms >= APP_FAULT_STARTUP_GRACE_MS;
  check_can(can, now_ms);
  /* A dying BMS times out for up to APP_CAN_STALE_MS before the link is declared down;
   * confirming later than that keeps an outage to the single BMS link entry. */
  counter_update(APP_FAULT_CAN_TIMEOUT,
                 &request_timeouts,
                 can->request_timeouts,
                 link_up,
                 APP_CAN_STALE_MS + 1000U,
                 now_ms);
  counter_update(APP_FAULT_CAN_REJECTED, &rejected_frames, can->rejected_frames, true, 0U, now_ms);
  for (unsigned i = 0U; i < APP_STATUS_SIGNAL_COUNT; ++i) {
    const app_status_signal_state_t *status = &aux->status_signals[i];
    check_input((uint16_t)(APP_FAULT_AUX_STATUS_FIRST + i),
                status->wired,
                status->valid,
                status->fault_active,
                now_ms);
  }
  check_input(APP_FAULT_SPEED, aux->speed_wired, aux->speed_valid, false, now_ms);
  check_input(APP_FAULT_MODE,
              aux->drive_mode_wired,
              aux->drive_mode_valid && aux->drive_mode < APP_DRIVE_MODE_COUNT,
              false,
              now_ms);
  check_storage(storage, now_ms);
  app_fault_history_flush();
  app_fault_journal_state_t journal = app_fault_history_journal_state();
  if (journal != APP_FAULT_JOURNAL_SAVED)
    episode_bad(APP_FAULT_HISTORY_STORAGE, APP_FAULT_STATE_ACTIVE, journal, journal, now_ms);
  else
    episode_good(APP_FAULT_HISTORY_STORAGE, 0U, APP_FAULT_CLEAR_HOLD_MS, now_ms);
}

/** Format a CAN measurement in display units for limit entries. */
static void describe_value(unsigned signal, uint32_t raw, char *text, uint32_t size) {
  int32_t value = (int32_t)(int16_t)(uint16_t)raw;
  uint32_t unsigned_value = raw & 0xFFFFU;
  switch (signal) {
  case APP_CAN_SIGNAL_PACK_VOLTAGE:
  case APP_CAN_SIGNAL_PACK_SUMMED_VOLTAGE:
    snprintf(text,
             size,
             "%lu.%lu V",
             (unsigned long)(unsigned_value / 10U),
             (unsigned long)(unsigned_value % 10U));
    break;
  case APP_CAN_SIGNAL_PACK_CURRENT:
    snprintf(text,
             size,
             "%s%ld.%ld A",
             value < 0 ? "-" : "",
             (long)((value < 0 ? -value : value) / 10),
             (long)((value < 0 ? -value : value) % 10));
    break;
  case APP_CAN_SIGNAL_SOC:
    snprintf(text, size, "%lu %%", (unsigned long)(unsigned_value & 0xFFU));
    break;
  case APP_CAN_SIGNAL_TEMP_HIGH:
  case APP_CAN_SIGNAL_TEMP_LOW:
  case APP_CAN_SIGNAL_TEMP_AVG:
    snprintf(text, size, "%d C", (int)(int8_t)(uint8_t)raw);
    break;
  default:
    snprintf(text, size, "%lu mV", (unsigned long)((unsigned_value + 5U) / 10U));
    break;
  }
}

/** Explain an event in words; unknown combinations fall back to raw state and detail. */
void app_fault_describe(const app_fault_event_t *event, char *text, uint32_t size) {
  if (text == NULL || size == 0U)
    return;
  if (event == NULL) {
    text[0] = '\0';
    return;
  }
  const char *name = app_fault_source_name(event->source);
  unsigned long detail = (unsigned long)event->detail;
  bool healthy = event->state == APP_FAULT_STATE_HEALTHY;
  bool active = event->state == APP_FAULT_STATE_ACTIVE;
  char value[24];
  if (event->source < APP_CAN_SIGNAL_COUNT) {
    if (healthy)
      snprintf(text, size, "%s: OK again", name);
    else if (active) {
      describe_value(event->source, event->detail, value, sizeof(value));
      snprintf(text,
               size,
               "%s: %s limit (%s)",
               name,
               (event->detail >> 16U) == APP_FAULT_LIMIT_BELOW ? "below" : "above",
               value);
    } else
      snprintf(text,
               size,
               "%s: %s",
               name,
               event->detail == APP_FAULT_SIGNAL_STOPPED ? "stopped updating" : "never received");
    return;
  }
  switch (event->source) {
  case APP_FAULT_BMS_LINK:
    snprintf(text,
             size,
             "BMS link: %s",
             healthy                                           ? "replies OK again"
             : event->detail == APP_FAULT_LINK_NEVER_CONNECTED ? "no reply since boot"
                                                               : "replies stopped");
    return;
  case APP_FAULT_CAN_TIMEOUT:
  case APP_FAULT_CAN_REJECTED:
    if (healthy)
      snprintf(text, size, "%s: stopped, %lu in episode", name, detail);
    else
      snprintf(text, size, "%s: started (total %lu)", name, detail);
    return;
  case APP_FAULT_STORAGE:
    if (healthy)
      snprintf(
          text, size, "Storage: OK again, %lu error%s in episode", detail, detail == 1U ? "" : "s");
    else
      snprintf(text,
               size,
               "Storage: %s",
               event->detail == APP_FAULT_STORAGE_NOT_READY ? "card not ready" : "write errors");
    return;
  case APP_FAULT_STORAGE_FULL:
    snprintf(text, size, "Telemetry storage: %s", healthy ? "space available" : "full");
    return;
  case APP_FAULT_HISTORY_STORAGE: {
    static const char *const reasons[] = {"saving",
                                          "no card, not saved",
                                          "write failing, retrying",
                                          "journal inconsistent, not writing",
                                          "journal full, not saved"};
    snprintf(text,
             size,
             "History storage: %s",
             healthy                                                ? "saving again"
             : event->detail < sizeof(reasons) / sizeof(reasons[0]) ? reasons[event->detail]
                                                                    : "unavailable");
    return;
  }
  case APP_FAULT_SPEED:
  case APP_FAULT_MODE:
    snprintf(text, size, "%s: %s", name, healthy ? "OK again" : "no signal");
    return;
  default:
    break;
  }
  if (event->source >= APP_FAULT_AUX_STATUS_FIRST &&
      event->source < APP_FAULT_AUX_STATUS_FIRST + APP_STATUS_SIGNAL_COUNT) {
    snprintf(text, size, "%s: %s", name, healthy ? "OK again" : active ? "fault" : "no signal");
    return;
  }
  snprintf(text, size, "%s: state %u [%lu]", name, (unsigned)event->state, detail);
}
