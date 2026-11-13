/* Exercise synthetic board-demo inputs without CAN, GPIO, LVGL or storage hardware. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "app_demo.h"
#include "app_fault_history.h"

/** Check the demo's motion, signal loss, recovery and volatile fault episodes. */
int main(void) {
  app_can_dashboard_data_t data = {0};
  app_aux_ui_state_t aux = {0};
  const uint32_t ticks[] = {0U, 10000U, 20000U, 30000U, 40000U, UINT32_MAX};
  const uint16_t speeds[] = {0U, 70U, 140U, 70U, 0U};
  for (unsigned i = 0; i < sizeof(ticks) / sizeof(ticks[0]); ++i) {
    app_demo_sample(ticks[i], &data, &aux);
    assert(data.valid_mask == (1U << APP_CAN_SIGNAL_COUNT) - 1U);
    assert(data.power_valid);
    for (unsigned signal = 0; signal < APP_CAN_SIGNAL_COUNT; ++signal)
      assert(data.signal_tick_ms[signal] == ticks[i]);
    if (i < sizeof(speeds) / sizeof(speeds[0]))
      assert(aux.speed_kph == speeds[i]);
    assert(aux.speed_valid && aux.drive_mode_valid && aux.status_inputs_initialized);
    for (unsigned signal = 0; signal < APP_STATUS_SIGNAL_COUNT; ++signal)
      assert(aux.status_signals[signal].valid && aux.status_signals[signal].wired);
    assert(data.rejected_frames == 0U && data.request_timeouts == 0U);
  }
  app_demo_sample(0U, &data, &aux);
  assert(data.pack_voltage_deci_volts == 3200U);
  assert(data.pack_current_deci_amps == -400);
  assert(data.pack_state_of_charge == 20U);
  app_demo_sample(20000U, &data, &aux);
  assert(data.pack_voltage_deci_volts == 4000U);
  assert(data.pack_current_deci_amps == 1600);
  assert(data.pack_state_of_charge == 90U);
  for (uint32_t now = 0U; now <= 80000U; now += 100U) {
    app_demo_sample(now, &data, &aux);
    assert(data.lowest_cell_voltage_100uv <= data.average_cell_voltage_100uv);
    assert(data.average_cell_voltage_100uv <= data.highest_cell_voltage_100uv);
    assert(data.lowest_temperature_c <= data.average_temperature_c);
    assert(data.average_temperature_c <= data.highest_temperature_c);
    assert(data.pack_current_deci_amps >= -400 && data.pack_current_deci_amps <= 1600);
    assert(data.pack_state_of_charge >= 20U && data.pack_state_of_charge <= 90U);
    assert(aux.speed_kph <= 140U);
    assert(aux.drive_mode < APP_DRIVE_MODE_COUNT);
  }
  app_can_dashboard_data_t before = data;
  app_aux_ui_state_t aux_before = aux;
  app_demo_sample(42U, NULL, &aux);
  app_demo_sample(42U, &data, NULL);
  assert(memcmp(&before, &data, sizeof(data)) == 0);
  assert(memcmp(&aux_before, &aux, sizeof(aux)) == 0);
  app_demo_sample(15000U, &data, &aux);
  assert(15000U - data.signal_tick_ms[APP_CAN_SIGNAL_PACK_VOLTAGE] > APP_CAN_STALE_MS);
  app_demo_sample(26000U, &data, &aux);
  assert((data.valid_mask & (1U << APP_CAN_SIGNAL_SOC)) == 0U);
  app_demo_history_init();
  for (uint32_t now = 0U; now < 80000U; now += 100U) {
    app_demo_sample(now, &data, &aux);
    app_demo_observe(&data, &aux, now);
  }
  bool saw_link = false, saw_soc = false, saw_imd = false, saw_clear = false;
  for (uint32_t i = 0; i < app_fault_history_count(); ++i) {
    app_fault_event_t event;
    assert(app_fault_history_get(i, &event));
    saw_link |= event.source == APP_FAULT_BMS_LINK;
    saw_soc |= event.source == APP_CAN_SIGNAL_SOC;
    saw_imd |= event.source == APP_FAULT_AUX_STATUS_FIRST + APP_STATUS_SIGNAL_IMD;
    saw_clear |= event.state == APP_FAULT_STATE_HEALTHY;
    assert(event.source != APP_FAULT_HISTORY_STORAGE);
  }
  assert(saw_link && saw_soc && saw_imd && saw_clear);
  assert(app_fault_history_pending() == 0U);
  return 0;
}
