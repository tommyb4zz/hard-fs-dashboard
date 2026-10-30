/**
 * File purpose: Define the integer-scaled CAN snapshot shared by acquisition, UI, RAM storage and
 * eMMC wire export, plus classic-CAN lifecycle and diagnostic controls. Runtime state includes
 * per-signal validity/age; the decoder does not populate reserved fault_flags.
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

/* Display freshness, not a vehicle safety limit. The complete poll cycle is ~500 ms. */
#define APP_CAN_STALE_MS 2000U
typedef enum {
  APP_CAN_SIGNAL_PACK_VOLTAGE = 0,
  APP_CAN_SIGNAL_PACK_SUMMED_VOLTAGE,
  APP_CAN_SIGNAL_PACK_CURRENT,
  APP_CAN_SIGNAL_SOC,
  APP_CAN_SIGNAL_TEMP_HIGH,
  APP_CAN_SIGNAL_TEMP_LOW,
  APP_CAN_SIGNAL_TEMP_AVG,
  APP_CAN_SIGNAL_CELL_LOW,
  APP_CAN_SIGNAL_CELL_HIGH,
  APP_CAN_SIGNAL_CELL_AVG,
  APP_CAN_SIGNAL_COUNT
} app_can_signal_id_t;

/**
 * Complete retained battery state. Voltage fields are unsigned 0.1 V; cell fields are
 * unsigned 100 microvolts; currents are signed 0.1 A; power statistics are signed whole
 * kW; SOC is integer percent; temperatures are signed whole degrees Celsius. Min/mean/max
 * current and power describe accepted samples since static initialization, not a time
 * window. The ten-PID decoder preserves all other fields and does not decode fault_flags.
 * valid_mask distinguishes missing/zero; signal_tick_ms records accepted-response age.
 */
/* Stable measurement-only prefix for telemetry archives. Runtime ages are not duplicated
 * in every RAM/eMMC log record; malfunction events have their own persistent journal. */
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
} app_can_measurements_t;

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
  uint16_t valid_mask;
  bool power_valid;
  uint32_t signal_tick_ms[APP_CAN_SIGNAL_COUNT];
  uint32_t rejected_frames;
  uint32_t request_timeouts;
} app_can_dashboard_data_t;

/** Return true only for a received signal no older than APP_CAN_STALE_MS; wrap-safe. */
bool app_can_signal_is_fresh(const app_can_dashboard_data_t *data,
                             app_can_signal_id_t signal,
                             uint32_t now_ms);
/** Return a stable human-readable signal name, or "Unknown" for an invalid index. */
const char *app_can_signal_name(app_can_signal_id_t signal);

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
 * idle. Copy the retained snapshot even on an empty initialized poll; unsigned elapsed-time
 * arithmetic supports tick rollover.
 *
 * data is caller storage for a full snapshot. Return true only if a reply is accepted. Capture HAL
 * milliseconds once; expire pending requests only when unsigned elapsed time is strictly greater
 * than 100 ms, then drain FIFO0 until empty or HAL read failure. Valid unsolicited/duplicate
 * replies still update measurements, while only a matching PID clears pending and advances. When
 * idle and elapsed time is at least 50 ms, enqueue the next request and record its time on success.
 * Return false for null/uninitialized input or no accepted reply. Consumers evaluate age on their
 * own clock.
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
