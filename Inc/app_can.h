/**
 * File purpose: Define the integer-scaled CAN snapshot shared by acquisition, UI, RAM storage and eMMC
 * wire export, plus classic-CAN lifecycle and diagnostic controls. Battery fields carry
 * no validity or per-signal age; the existing decoder does not populate fault_flags.
 * Consumers must preserve these units and signed types rather than interpret zero as
 * confirmed freshness.
 */
#ifndef APP_CAN_H
#define APP_CAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * Complete retained battery state. Voltage fields are unsigned 0.1 V; cell fields are
 * unsigned 100 microvolts; currents are signed 0.1 A; power statistics are signed whole
 * kW; SOC is integer percent; temperatures are signed whole degrees Celsius. Min/mean/max
 * current and power describe accepted samples since static initialization, not a time
 * window. The ten-PID decoder preserves all other fields and does not decode fault_flags.
 * No field here distinguishes absent, stale, invalid or initial-zero battery data.
 */
typedef struct {
  uint16_t pack_voltage_deci_volts;
  uint16_t pack_summed_voltage_deci_volts;
  uint16_t lowest_cell_voltage_100uv;
  uint16_t average_cell_voltage_100uv;
  uint16_t highest_cell_voltage_100uv;
  int16_t pack_current_deci_amps;
  int16_t lowest_pack_current_deci_amps;
  int16_t average_pack_current_deci_amps;
  int16_t highest_pack_current_deci_amps;
  int16_t lowest_pack_power_kw;
  int16_t average_pack_power_kw;
  int16_t highest_pack_power_kw;
  uint8_t pack_state_of_charge;
  int8_t highest_temperature_c;
  int8_t average_temperature_c;
  int8_t lowest_temperature_c;
  uint8_t fault_flags;
} app_can_dashboard_data_t;

/**
 * Initialize FDCAN using the board configuration and response-ID filter; report failures
 * for caller retry.
 *
 * With no arguments, return true immediately if already initialized. Select HSE, normal
 * classic mode and automatic retransmission; allocate one exact-ID standard filter, eight
 * RX FIFO0 slots and one TX FIFO slot. Clock/init/filter/start failures return false for
 * task retry. Only full success marks ready and resets request position/pending/time;
 * existing measurement/statistics storage is not cleared. At the configured 25 MHz HSE,
 * prescaler 5 with 1+8+1 time quanta calculates 500 kbit/s.
 */
bool app_can_init(void);
/**
 * Expire overdue requests, drain valid replies, and send the next PID every 50 ms when
 * idle. Copy the latest snapshot only after a valid reply; unsigned elapsed-time
 * arithmetic supports tick rollover.
 *
 * data is caller storage for a full snapshot and is written only if at least one reply is
 * accepted. Capture HAL milliseconds once; expire pending requests only when unsigned
 * elapsed time is strictly greater than 100 ms, then drain FIFO0 until empty or HAL read
 * failure. Valid unsolicited/duplicate replies still update measurements, while only a
 * matching PID clears pending and advances. When idle and elapsed time is at least 50 ms,
 * enqueue the next request and record its time on success. Return false for
 * null/uninitialized input or no accepted reply; no battery-staleness transition is
 * produced.
 */
bool app_can_poll_dashboard_data(app_can_dashboard_data_t *data);
/**
 * Enable or disable CAN initialization, request, response and timeout serial diagnostics.
 *
 * enabled replaces the module-wide boolean consulted by initialization, TX, RX and timeout
 * printers. It has no return value and does not stop CAN requests or protect against
 * concurrent console output.
 */
void app_can_set_debug_enabled(bool enabled);
/**
 * Return the current CAN diagnostic-output setting.
 *
 * Return the module-wide debug boolean without side effects. DEBUG builds start with true,
 * other builds false; later calls to the setter override that default.
 */
bool app_can_is_debug_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAN_H */
