/* Synthetic engineering values for an explicitly selected, visibly labelled board demo. */
#include "app_demo.h"
#include "app_fault_monitor.h"

#include <string.h>

/* Volatile demo history is separate from the eMMC journal and disappears at reset. */
static app_fault_event_t demo_journal[APP_FAULT_HISTORY_CAPACITY];
static uint32_t demo_journal_count;
static bool demo_history_initialized;

/** Fill one synthetic sample, including scheduled stale/missing data and recovery. */
void app_demo_sample(uint32_t now_ms, app_can_dashboard_data_t *data, app_aux_ui_state_t *aux) {
  if (data == NULL || aux == NULL)
    return;
  memset(data, 0, sizeof(*data));
  memset(aux, 0, sizeof(*aux));
  uint32_t phase = (now_ms / 100U) % 400U;
  uint32_t ramp = phase <= 200U ? phase : 400U - phase;
  data->pack_voltage_deci_volts = (uint16_t)(3200U + 4U * ramp);
  data->pack_summed_voltage_deci_volts = data->pack_voltage_deci_volts - 2U;
  data->pack_current_deci_amps = (int16_t)(((int32_t)ramp - 40) * 10);
  data->pack_state_of_charge = (uint8_t)(20U + 70U * ramp / 200U);
  data->highest_temperature_c = (int8_t)(25U + 30U * ramp / 200U);
  data->average_temperature_c = data->highest_temperature_c - 3;
  data->lowest_temperature_c = data->highest_temperature_c - 6;
  data->average_cell_voltage_100uv = (uint16_t)(33000U + 35U * ramp);
  data->lowest_cell_voltage_100uv = data->average_cell_voltage_100uv - 300U;
  data->highest_cell_voltage_100uv = data->average_cell_voltage_100uv + 300U;
  data->lowest_pack_current_deci_amps = -400;
  data->average_pack_current_deci_amps = data->pack_current_deci_amps;
  data->highest_pack_current_deci_amps = 1600;
  int32_t power = (int32_t)data->pack_current_deci_amps * data->pack_voltage_deci_volts;
  data->average_pack_power_kw = (int16_t)((power + (power < 0 ? -50000 : 50000)) / 100000);
  data->lowest_pack_power_kw = -13;
  data->highest_pack_power_kw = 64;
  data->power_valid = true;
  data->valid_mask = (1U << APP_CAN_SIGNAL_COUNT) - 1U;
  for (unsigned i = 0; i < APP_CAN_SIGNAL_COUNT; ++i)
    data->signal_tick_ms[i] = now_ms;
  /* A six-second BMS pause lets the real freshness display and link monitor react. */
  if (phase >= 120U && phase < 180U) {
    uint32_t paused_at = now_ms - (now_ms % 40000U) + 12000U;
    for (unsigned i = 0; i < APP_CAN_SIGNAL_COUNT; ++i)
      data->signal_tick_ms[i] = paused_at;
  }
  if (phase >= 250U && phase < 290U)
    data->valid_mask &= (uint16_t) ~(1U << APP_CAN_SIGNAL_SOC);
  aux->speed_kph = (uint16_t)(140U * ramp / 200U);
  aux->drive_mode = aux->speed_kph == 0U ? APP_DRIVE_MODE_NEUTRAL : APP_DRIVE_MODE_DRIVE;
  aux->speed_valid = aux->drive_mode_valid = true;
  aux->speed_wired = aux->drive_mode_wired = true;
  aux->status_inputs_initialized = true;
  for (unsigned i = 0; i < APP_STATUS_SIGNAL_COUNT; ++i) {
    aux->status_signals[i].valid = true;
    aux->status_signals[i].wired = true;
  }
  /* Show a synthetic indicator episode each cycle; this is never written to history. */
  aux->status_signals[APP_STATUS_SIGNAL_IMD].fault_active = phase >= 360U;
}

/** Read an immutable RAM slot or report untouched storage, just as the real journal does. */
static int app_demo_read(uint32_t index, app_fault_event_t *event) {
  if (index >= APP_FAULT_HISTORY_CAPACITY || event == NULL)
    return APP_FAULT_SLOT_IO_ERROR;
  if (index >= demo_journal_count)
    return APP_FAULT_SLOT_ERASED;
  *event = demo_journal[index];
  return APP_FAULT_SLOT_VALID;
}

/** Append a RAM event once; refuse occupied slots and finite-capacity overflow. */
static bool app_demo_write(uint32_t index, const app_fault_event_t *event) {
  if (event == NULL || index != demo_journal_count || index >= APP_FAULT_HISTORY_CAPACITY)
    return false;
  demo_journal[demo_journal_count++] = *event;
  return true;
}

/** Initialize the optional RAM-only history backend for synthetic episode tests. */
void app_demo_history_init(void) {
  if (demo_history_initialized)
    return;
  demo_history_initialized = true;
  app_fault_history_init(1U);
  app_fault_history_attach(app_demo_read, app_demo_write, APP_FAULT_HISTORY_CAPACITY);
}

/** Pass synthetic inputs to the episode monitor using the RAM-only history backend. */
void app_demo_observe(const app_can_dashboard_data_t *data,
                      const app_aux_ui_state_t *aux,
                      uint32_t now_ms) {
  /* The simulated backend is RAM; this describes the scenario, not the physical eMMC. */
  const app_emmc_log_status_t storage = {.ready = true};
  app_fault_monitor_update(data, aux, &storage, now_ms);
}
