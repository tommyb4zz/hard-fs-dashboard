/**
 * File purpose: Configure the board classic-CAN controller, issue the ten implemented diagnostic
 * PID requests, validate/decode replies into integer-scaled dashboard state, and accumulate
 * lifetime current/power statistics. All module state is private and assumes serialized
 * application polling; the host reuses this logic behind HAL fakes. Vehicle
 * schema/freshness and controller recovery policies are not supplied here.
 */
#include "app_can.h"

#include <stdio.h>

#include "app_can_config.h"
#include "main.h"

static FDCAN_HandleTypeDef app_hfdcan;
static bool app_can_initialized = false;
/* Existing implemented diagnostic contract: standard identifiers, 0x22 request/0x62 reply.
 * These are software constants, not verified requirements for the future vehicle bus. */
#define APP_CAN_ORION_REQUEST_ID 0x7E3U
#define APP_CAN_ORION_RESPONSE_ID 0x7EBU
#define APP_CAN_OBD2_MODE_READ_DATA_BY_IDENTIFIER 0x22U
#define APP_CAN_OBD2_MODE_READ_DATA_RESPONSE 0x62U

#define APP_CAN_PID_PACK_VOLTAGE 0xF00DU
#define APP_CAN_PID_PACK_SUMMED_VOLTAGE 0xF014U
#define APP_CAN_PID_PACK_SOC 0xF00FU
#define APP_CAN_PID_PACK_CURRENT 0xF015U
#define APP_CAN_PID_HIGHEST_TEMPERATURE 0xF028U
#define APP_CAN_PID_LOWEST_TEMPERATURE 0xF029U
#define APP_CAN_PID_AVERAGE_TEMPERATURE 0xF02AU
#define APP_CAN_PID_LOWEST_CELL_VOLTAGE 0xF032U
#define APP_CAN_PID_HIGHEST_CELL_VOLTAGE 0xF033U
#define APP_CAN_PID_AVERAGE_CELL_VOLTAGE 0xF034U

#define APP_CAN_REQUEST_PERIOD_MS 50U
#define APP_CAN_REQUEST_TIMEOUT_MS 100U
/* HAL copies payload bytes before application validation; the largest DLC represents 64 bytes. */
#define APP_CAN_MAX_RX_PAYLOAD_BYTES 64U

/* Round-robin order: voltage, summed voltage, current, SOC, three temperatures, three cells. */
typedef enum {
  APP_CAN_REQUEST_PACK_VOLTAGE = 0,
  APP_CAN_REQUEST_PACK_SUMMED_VOLTAGE,
  APP_CAN_REQUEST_PACK_CURRENT,
  APP_CAN_REQUEST_PACK_SOC,
  APP_CAN_REQUEST_HIGHEST_TEMPERATURE,
  APP_CAN_REQUEST_LOWEST_TEMPERATURE,
  APP_CAN_REQUEST_AVERAGE_TEMPERATURE,
  APP_CAN_REQUEST_LOWEST_CELL_VOLTAGE,
  APP_CAN_REQUEST_HIGHEST_CELL_VOLTAGE,
  APP_CAN_REQUEST_AVERAGE_CELL_VOLTAGE,
  APP_CAN_REQUEST_COUNT
} app_can_request_index_t;

/* Expected PCI is bytes after the PCI: service + two PID bytes + one/two measurement bytes. */
typedef struct {
  uint16_t pid;
  uint8_t expected_obd_length;
} app_can_request_definition_t;

static const app_can_request_definition_t app_can_request_table[APP_CAN_REQUEST_COUNT] = {
    [APP_CAN_REQUEST_PACK_VOLTAGE] =
        {
            .pid = APP_CAN_PID_PACK_VOLTAGE,
            .expected_obd_length = 5U,
        },
    [APP_CAN_REQUEST_PACK_SUMMED_VOLTAGE] =
        {
            .pid = APP_CAN_PID_PACK_SUMMED_VOLTAGE,
            .expected_obd_length = 5U,
        },
    [APP_CAN_REQUEST_PACK_CURRENT] =
        {
            .pid = APP_CAN_PID_PACK_CURRENT,
            .expected_obd_length = 5U,
        },
    [APP_CAN_REQUEST_PACK_SOC] =
        {
            .pid = APP_CAN_PID_PACK_SOC,
            .expected_obd_length = 4U,
        },
    [APP_CAN_REQUEST_HIGHEST_TEMPERATURE] =
        {
            .pid = APP_CAN_PID_HIGHEST_TEMPERATURE,
            .expected_obd_length = 4U,
        },
    [APP_CAN_REQUEST_LOWEST_TEMPERATURE] =
        {
            .pid = APP_CAN_PID_LOWEST_TEMPERATURE,
            .expected_obd_length = 4U,
        },
    [APP_CAN_REQUEST_AVERAGE_TEMPERATURE] =
        {
            .pid = APP_CAN_PID_AVERAGE_TEMPERATURE,
            .expected_obd_length = 4U,
        },
    [APP_CAN_REQUEST_LOWEST_CELL_VOLTAGE] =
        {
            .pid = APP_CAN_PID_LOWEST_CELL_VOLTAGE,
            .expected_obd_length = 5U,
        },
    [APP_CAN_REQUEST_HIGHEST_CELL_VOLTAGE] =
        {
            .pid = APP_CAN_PID_HIGHEST_CELL_VOLTAGE,
            .expected_obd_length = 5U,
        },
    [APP_CAN_REQUEST_AVERAGE_CELL_VOLTAGE] =
        {
            .pid = APP_CAN_PID_AVERAGE_CELL_VOLTAGE,
            .expected_obd_length = 5U,
        },
};

/* Retained state starts at zero and has no per-field validity/age. A pending PID tracks
 * scheduler progress only; accepted unsolicited replies may still change measurements. */
static app_can_dashboard_data_t app_can_latest_data = {0};
static app_can_request_index_t app_can_next_request = APP_CAN_REQUEST_PACK_VOLTAGE;
static uint16_t app_can_pending_pid = 0U;
static uint32_t app_can_last_request_ms = 0U;
#ifdef DEBUG
static bool app_can_debug_enabled = true;
#else
static bool app_can_debug_enabled = false;
#endif
static bool app_can_current_stats_valid = false;
static int16_t app_can_current_min_deci_amps = 0;
static int16_t app_can_current_max_deci_amps = 0;
static int64_t app_can_current_sum_deci_amps = 0;
static uint32_t app_can_current_sample_count = 0U;
static bool app_can_power_stats_valid = false;
static int16_t app_can_power_min_kw = 0;
static int16_t app_can_power_max_kw = 0;
static int64_t app_can_power_sum_kw = 0;
static uint32_t app_can_power_sample_count = 0U;

/** Check per-signal received validity and unsigned millisecond age. */
bool app_can_signal_is_fresh(const app_can_dashboard_data_t *data,
                             app_can_signal_id_t signal,
                             uint32_t now_ms) {
  return data != NULL && (unsigned)signal < APP_CAN_SIGNAL_COUNT &&
         (data->valid_mask & (1U << signal)) != 0U &&
         (uint32_t)(now_ms - data->signal_tick_ms[signal]) <= APP_CAN_STALE_MS;
}

/** Provide names for diagnostics without assigning new vehicle protocol fields. */
const char *app_can_signal_name(app_can_signal_id_t signal) {
  static const char *const names[] = {"Pack voltage",
                                      "Summed voltage",
                                      "Pack current",
                                      "State of charge",
                                      "High temperature",
                                      "Low temperature",
                                      "Avg temperature",
                                      "Low cell voltage",
                                      "High cell voltage",
                                      "Avg cell voltage"};
  return (unsigned)signal < APP_CAN_SIGNAL_COUNT ? names[signal] : "Unknown";
}

static int16_t app_can_decode_orion_current_deci_amps(uint16_t raw_value);
static void app_can_update_current_stats(app_can_dashboard_data_t *data);
static void app_can_update_power_stats(app_can_dashboard_data_t *data);

/**
 * Report the failing initialization stage when debug output is enabled and return false.
 *
 * stage names the failed setup operation. The return is always false so initialization
 * callers can propagate failure directly; printing also reads the handle error code. It
 * does not reset hardware or clear previously decoded data.
 */
static bool app_can_log_init_failure(const char *stage) {
  if (app_can_debug_enabled && (stage != NULL)) {
    printf("[can] init failed stage=%s fdcan_error=0x%08lX\r\n",
           stage,
           (unsigned long)app_hfdcan.ErrorCode);
  }

  return false;
}

/**
 * Return an absolute value for the signed int16-range values passed by the debug
 * formatter.
 *
 * The result is a signed 32-bit magnitude used only for decimal formatting. Callers widen
 * int16_t first, including -32768; the helper itself does not guard a general INT32_MIN
 * input and has no state changes.
 */
static int32_t app_can_abs_int32(int32_t value) {
  return (value < 0) ? -value : value;
}

/**
 * Saturate a wide signed result to the dashboard signed 16-bit storage range.
 *
 * Accept a signed 64-bit arithmetic result and return INT16_MIN/INT16_MAX at the limits,
 * otherwise cast exactly. This bounds stored current means and power values without
 * changing any accumulator.
 */
static int16_t app_can_clamp_int64_to_int16(int64_t value) {
  if (value > INT16_MAX) {
    return INT16_MAX;
  }

  if (value < INT16_MIN) {
    return INT16_MIN;
  }

  return (int16_t)value;
}

/**
 * Print a labeled decivolt value with one decimal digit; ignore a null label.
 *
 * One input count means 0.1 V. Integer quotient and remainder produce a decimal V suffix
 * without floating point; output has a leading space and no trailing newline. The snapshot
 * is unchanged.
 */
static void app_can_print_unsigned_deci_volts(const char *label, uint16_t deci_volts) {
  if (label == NULL) {
    return;
  }

  printf(" %s=%u.%uV", label, (unsigned int)(deci_volts / 10U), (unsigned int)(deci_volts % 10U));
}

/**
 * Print a labeled cell-voltage value in 100-microvolt units with four decimal digits.
 *
 * One input count means 0.0001 V. Divide by 10000 and zero-pad the remainder to four
 * digits; a null label produces no output. This is formatting only and does not validate
 * the measured range.
 */
static void app_can_print_unsigned_100uv_volts(const char *label, uint16_t voltage_100uv) {
  if (label == NULL) {
    return;
  }

  printf(" %s=%u.%04uV",
         label,
         (unsigned int)(voltage_100uv / 10000U),
         (unsigned int)(voltage_100uv % 10000U));
}

/**
 * Print signed deciamps as a labeled amperage with one decimal digit.
 *
 * One input count means 0.1 A. Widen before taking magnitude, preserve the sign
 * separately, and print whole/fractional parts with A; null label returns before output.
 * No statistics or sample fields change.
 */
static void app_can_print_signed_deci_amps(const char *label, int16_t deci_amps) {
  int32_t abs_deci_amps = 0;

  if (label == NULL) {
    return;
  }

  abs_deci_amps = app_can_abs_int32((int32_t)deci_amps);
  printf(" %s=%s%ld.%ldA",
         label,
         (deci_amps < 0) ? "-" : "",
         (long)(abs_deci_amps / 10),
         (long)(abs_deci_amps % 10));
}

/**
 * Prefer nonzero pack voltage, otherwise summed pack voltage; return zero for null data.
 *
 * The pointer is a retained CAN snapshot; return units remain 0.1 V. Zero is treated as a
 * fallback trigger rather than as validity metadata. Neither source has an age check, and
 * this function writes no state.
 */
static uint16_t app_can_get_display_voltage_deci_volts(const app_can_dashboard_data_t *data) {
  if (data == NULL) {
    return 0U;
  }

  if (data->pack_voltage_deci_volts != 0U) {
    return data->pack_voltage_deci_volts;
  }

  return data->pack_summed_voltage_deci_volts;
}

/**
 * Convert deciamps times decivolts to integer kW, rounding half values away from zero.
 *
 * Both inputs are integer-scaled samples: current in 0.1 A and voltage in 0.1 V. A 64-bit
 * product avoids intermediate overflow; division by 100000 converts to kW and the returned
 * int16_t saturates if needed. This computes instantaneous power, not integrated energy.
 */
static int16_t app_can_compute_power_kw(int16_t current_deci_amps, uint16_t voltage_deci_volts) {
  int64_t numerator = (int64_t)current_deci_amps * (int64_t)voltage_deci_volts;

  /* deci-amps * deci-volts = 0.01 W per count, divided by 100000 for kW.
   * Add/subtract half the divisor first to round to the nearest integer kW. */

  if (numerator >= 0) {
    return app_can_clamp_int64_to_int16((numerator + 50000) / 100000);
  }

  return app_can_clamp_int64_to_int16((numerator - 50000) / 100000);
}

/**
 * Print a labeled signed integer when a label is supplied.
 *
 * value is printed in signed decimal without a units suffix or newline. The debug caller
 * supplies the meaning through label; null label is a no-op and all measurement/statistics
 * state remains unchanged.
 */
static void app_can_print_signed_integer(const char *label, int16_t value) {
  if (label == NULL) {
    return;
  }

  printf(" %s=%d", label, (int)value);
}

/**
 * Log a PID submitted for transmission only when CAN debug output is enabled.
 *
 * pid is the 16-bit identifier just accepted by the HAL TX queue, printed in hexadecimal.
 * This message does not establish delivery or a CAN acknowledgement; no pending-request
 * fields are changed here.
 */
static void app_can_debug_log_request(uint16_t pid) {
  if (!app_can_debug_enabled) {
    return;
  }
  printf("[can] tx pid=0x%04X\r\n", pid);
}

/**
 * Log an outstanding PID that exceeded its timeout when CAN debug output is enabled.
 *
 * pid is the scheduler identifier whose elapsed request age exceeded the configured
 * timeout. This helper only emits text; the polling caller clears pending state and
 * advances the sequence.
 */
static void app_can_debug_log_timeout(uint16_t pid) {
  if (!app_can_debug_enabled) {
    return;
  }
  printf("[can] timeout pid=0x%04X\r\n", pid);
}

/**
 * Log a validated eight-byte reply and its decoded measurement/statistics when debug is
 * enabled.
 *
 * rx_data must address eight already-validated bytes and data must point to the post-
 * decode snapshot. Select the matching PID field, add current/power statistics for current
 * replies, and print fault_flags. Null pointers or disabled debugging produce no output;
 * neither input is mutated.
 */
static void app_can_debug_log_response(uint16_t pid,
                                       const uint8_t *rx_data,
                                       const app_can_dashboard_data_t *data) {
  if ((rx_data == NULL) || (data == NULL)) {
    return;
  }

  if (!app_can_debug_enabled) {
    return;
  }

  printf("[can] rx pid=0x%04X raw=%02X %02X %02X %02X %02X %02X %02X %02X",
         pid,
         rx_data[0],
         rx_data[1],
         rx_data[2],
         rx_data[3],
         rx_data[4],
         rx_data[5],
         rx_data[6],
         rx_data[7]);

  switch (pid) {
  case APP_CAN_PID_PACK_VOLTAGE:
    app_can_print_unsigned_deci_volts("pack_voltage", data->pack_voltage_deci_volts);
    break;

  case APP_CAN_PID_PACK_SUMMED_VOLTAGE:
    app_can_print_unsigned_deci_volts("pack_summed_voltage", data->pack_summed_voltage_deci_volts);
    break;

  case APP_CAN_PID_PACK_CURRENT:
    app_can_print_signed_deci_amps("pack_current", data->pack_current_deci_amps);
    app_can_print_signed_deci_amps("current_low", data->lowest_pack_current_deci_amps);
    app_can_print_signed_deci_amps("current_avg", data->average_pack_current_deci_amps);
    app_can_print_signed_deci_amps("current_high", data->highest_pack_current_deci_amps);
    app_can_print_signed_integer("power_low_kw", data->lowest_pack_power_kw);
    app_can_print_signed_integer("power_avg_kw", data->average_pack_power_kw);
    app_can_print_signed_integer("power_high_kw", data->highest_pack_power_kw);
    break;

  case APP_CAN_PID_PACK_SOC:
    printf(" soc=%u%%", (unsigned int)data->pack_state_of_charge);
    break;

  case APP_CAN_PID_HIGHEST_TEMPERATURE:
    printf(" highest_temp=%dC", (int)data->highest_temperature_c);
    break;

  case APP_CAN_PID_LOWEST_TEMPERATURE:
    printf(" lowest_temp=%dC", (int)data->lowest_temperature_c);
    break;

  case APP_CAN_PID_AVERAGE_TEMPERATURE:
    printf(" average_temp=%dC", (int)data->average_temperature_c);
    break;

  case APP_CAN_PID_LOWEST_CELL_VOLTAGE:
    app_can_print_unsigned_100uv_volts("cell_voltage_low", data->lowest_cell_voltage_100uv);
    break;

  case APP_CAN_PID_HIGHEST_CELL_VOLTAGE:
    app_can_print_unsigned_100uv_volts("cell_voltage_high", data->highest_cell_voltage_100uv);
    break;

  case APP_CAN_PID_AVERAGE_CELL_VOLTAGE:
    app_can_print_unsigned_100uv_volts("cell_voltage_avg", data->average_cell_voltage_100uv);
    break;

  default:
    break;
  }

  printf(" flags=0x%02X\r\n", data->fault_flags);
}

/**
 * Enable one supported GPIO A-I peripheral clock, returning false for another port.
 *
 * port is a HAL GPIO register pointer. The A through I pointer comparisons select exactly
 * one RCC clock-enable macro; success means a supported port was selected, not a pin or
 * transceiver test. No HAL error result is available from these macros.
 */
static bool app_can_enable_gpio_clock(GPIO_TypeDef *port) {
  if (port == GPIOA) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    return true;
  }
  if (port == GPIOB) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    return true;
  }
  if (port == GPIOC) {
    __HAL_RCC_GPIOC_CLK_ENABLE();
    return true;
  }
  if (port == GPIOD) {
    __HAL_RCC_GPIOD_CLK_ENABLE();
    return true;
  }
  if (port == GPIOE) {
    __HAL_RCC_GPIOE_CLK_ENABLE();
    return true;
  }
  if (port == GPIOF) {
    __HAL_RCC_GPIOF_CLK_ENABLE();
    return true;
  }
  if (port == GPIOG) {
    __HAL_RCC_GPIOG_CLK_ENABLE();
    return true;
  }
  if (port == GPIOH) {
    __HAL_RCC_GPIOH_CLK_ENABLE();
    return true;
  }
  if (port == GPIOI) {
    __HAL_RCC_GPIOI_CLK_ENABLE();
    return true;
  }

  return false;
}

/**
 * Enable or disable CAN initialization, request, response and timeout serial diagnostics.
 *
 * enabled replaces the module-wide boolean consulted by initialization, TX, RX and timeout
 * printers. It has no return value and does not stop CAN requests or protect against
 * concurrent console output.
 */
void app_can_set_debug_enabled(bool enabled) {
  app_can_debug_enabled = enabled;
}

/**
 * Return the current CAN diagnostic-output setting.
 *
 * Return the module-wide debug boolean without side effects. DEBUG builds start with true,
 * other builds false; later calls to the setter override that default.
 */
bool app_can_is_debug_enabled(void) {
  return app_can_debug_enabled;
}

/**
 * Accept only a nonnull configuration selecting the implemented classic CAN mode.
 *
 * Only mode is examined after the null guard: APP_CAN_MODE_FD is declared in the interface
 * but not implemented here. Instance validity, pins and electrical compatibility are not
 * checked by this predicate.
 */
static bool app_can_config_is_supported(const app_can_config_t *config) {
  if (config == NULL) {
    return false;
  }

  return config->mode == APP_CAN_MODE_CLASSIC;
}

/**
 * Limit a decoded unsigned state-of-charge percentage to 100.
 *
 * value is an unsigned integer percent after the SOC decoder conversion. Values 0 through
 * 100 pass through; larger values return 100. It neither reports invalid raw data nor
 * attaches a validity flag.
 */
static uint8_t app_can_clamp_percentage(uint8_t value) {
  if (value > 100U) {
    return 100U;
  }

  return value;
}

/**
 * Subtract the implemented 32767 raw-current offset and saturate to signed deciamps.
 *
 * The existing protocol interprets the big-endian raw 16-bit word as an offset-binary
 * number with zero at 32767. Return units are 0.1 A; raw 65535 would produce 32768 and is
 * therefore clipped to 32767. This is the implemented encoding, not confirmation of a
 * future BMS protocol.
 */
static int16_t app_can_decode_orion_current_deci_amps(uint16_t raw_value) {
  int32_t current_deci_amps = (int32_t)raw_value - 32767;

  if (current_deci_amps > INT16_MAX) {
    return INT16_MAX;
  }

  if (current_deci_amps < INT16_MIN) {
    return INT16_MIN;
  }

  return (int16_t)current_deci_amps;
}

/**
 * Accumulate accepted current samples into signed min/max and nearest-integer mean values.
 *
 * data must contain the newly decoded current in 0.1 A. The first sample seeds persistent
 * min/max/sum/count; subsequent samples update them and write rounded signed mean/min/max
 * back into the snapshot. Every accepted current reply counts, including duplicates; there
 * is no time window, session reset or count-overflow handling in this helper.
 */
static void app_can_update_current_stats(app_can_dashboard_data_t *data) {
  int64_t average_deci_amps = 0;

  if (data == NULL) {
    return;
  }

  if (!app_can_current_stats_valid) {
    app_can_current_stats_valid = true;
    app_can_current_min_deci_amps = data->pack_current_deci_amps;
    app_can_current_max_deci_amps = data->pack_current_deci_amps;
    app_can_current_sum_deci_amps = (int64_t)data->pack_current_deci_amps;
    app_can_current_sample_count = 1U;
  } else {
    if (data->pack_current_deci_amps < app_can_current_min_deci_amps) {
      app_can_current_min_deci_amps = data->pack_current_deci_amps;
    }

    if (data->pack_current_deci_amps > app_can_current_max_deci_amps) {
      app_can_current_max_deci_amps = data->pack_current_deci_amps;
    }

    app_can_current_sum_deci_amps += (int64_t)data->pack_current_deci_amps;
    app_can_current_sample_count++;
  }

  average_deci_amps = app_can_current_sum_deci_amps;
  if (average_deci_amps >= 0) {
    average_deci_amps = (average_deci_amps + ((int64_t)app_can_current_sample_count / 2)) /
                        (int64_t)app_can_current_sample_count;
  } else {
    average_deci_amps = (average_deci_amps - ((int64_t)app_can_current_sample_count / 2)) /
                        (int64_t)app_can_current_sample_count;
  }

  data->lowest_pack_current_deci_amps = app_can_current_min_deci_amps;
  data->highest_pack_current_deci_amps = app_can_current_max_deci_amps;
  data->average_pack_current_deci_amps = app_can_clamp_int64_to_int16(average_deci_amps);
}

/**
 * Accumulate integer-kW min/max/mean at current-sample time when voltage is fresh.
 *
 * Use fresh pack voltage, or fresh summed voltage, with the newly decoded current.
 * Round each power sample to integer kW before maintaining persistent min/max/sum/count
 * and writing the rounded mean. Null data or absent/stale voltage leave statistics
 * unchanged. Sampling has no freshness pairing or time integration.
 */
static void app_can_update_power_stats(app_can_dashboard_data_t *data) {
  uint16_t voltage_deci_volts = 0U;
  int16_t power_kw = 0;
  int64_t average_power_kw = 0;

  if (data == NULL) {
    return;
  }

  voltage_deci_volts = app_can_get_display_voltage_deci_volts(data);
  if (app_can_signal_is_fresh(data, APP_CAN_SIGNAL_PACK_VOLTAGE, HAL_GetTick())) {
    voltage_deci_volts = data->pack_voltage_deci_volts;
  } else if (app_can_signal_is_fresh(data, APP_CAN_SIGNAL_PACK_SUMMED_VOLTAGE, HAL_GetTick())) {
    voltage_deci_volts = data->pack_summed_voltage_deci_volts;
  } else {
    data->power_valid = false;
    return;
  }

  data->power_valid = true;
  power_kw = app_can_compute_power_kw(data->pack_current_deci_amps, voltage_deci_volts);

  if (!app_can_power_stats_valid) {
    app_can_power_stats_valid = true;
    app_can_power_min_kw = power_kw;
    app_can_power_max_kw = power_kw;
    app_can_power_sum_kw = (int64_t)power_kw;
    app_can_power_sample_count = 1U;
  } else {
    if (power_kw < app_can_power_min_kw) {
      app_can_power_min_kw = power_kw;
    }

    if (power_kw > app_can_power_max_kw) {
      app_can_power_max_kw = power_kw;
    }

    app_can_power_sum_kw += (int64_t)power_kw;
    app_can_power_sample_count++;
  }

  average_power_kw = app_can_power_sum_kw;
  if (average_power_kw >= 0) {
    average_power_kw = (average_power_kw + ((int64_t)app_can_power_sample_count / 2)) /
                       (int64_t)app_can_power_sample_count;
  } else {
    average_power_kw = (average_power_kw - ((int64_t)app_can_power_sample_count / 2)) /
                       (int64_t)app_can_power_sample_count;
  }

  data->lowest_pack_power_kw = app_can_power_min_kw;
  data->highest_pack_power_kw = app_can_power_max_kw;
  data->average_pack_power_kw = app_can_clamp_int64_to_int16(average_power_kw);
}

/**
 * Return the PID at the current position in the ten-request round-robin table.
 *
 * The private index must remain in the request-table range. Return the table PID without
 * advancing it or changing pending state; initialization and the advance helper establish
 * that invariant.
 */
static uint16_t app_can_get_next_request_pid(void) {
  return app_can_request_table[app_can_next_request].pid;
}

/**
 * Advance the ten-request round-robin position and wrap from the final PID to the first.
 *
 * Increment the private request enum modulo APP_CAN_REQUEST_COUNT. The polling caller
 * invokes this only on timeout or a reply matching the pending PID; sending a request does
 * not advance it.
 */
static void app_can_advance_request_sequence(void) {
  app_can_next_request =
      (app_can_request_index_t)((app_can_next_request + 1U) % APP_CAN_REQUEST_COUNT);
}

/**
 * Translate a HAL DLC constant to its payload-byte count; unknown constants map to zero.
 *
 * data_length_code uses HAL-encoded DLC constants, not a byte count. Mapping includes
 * legal FD lengths up to 64 so the receiver can reject them after HAL has copied into the
 * 64-byte scratch buffer. This conversion does not itself accept a frame.
 */
static uint8_t app_can_dlc_to_bytes(uint32_t data_length_code) {
  switch (data_length_code) {
  case FDCAN_DLC_BYTES_0:
    return 0U;
  case FDCAN_DLC_BYTES_1:
    return 1U;
  case FDCAN_DLC_BYTES_2:
    return 2U;
  case FDCAN_DLC_BYTES_3:
    return 3U;
  case FDCAN_DLC_BYTES_4:
    return 4U;
  case FDCAN_DLC_BYTES_5:
    return 5U;
  case FDCAN_DLC_BYTES_6:
    return 6U;
  case FDCAN_DLC_BYTES_7:
    return 7U;
  case FDCAN_DLC_BYTES_8:
    return 8U;
  case FDCAN_DLC_BYTES_12:
    return 12U;
  case FDCAN_DLC_BYTES_16:
    return 16U;
  case FDCAN_DLC_BYTES_20:
    return 20U;
  case FDCAN_DLC_BYTES_24:
    return 24U;
  case FDCAN_DLC_BYTES_32:
    return 32U;
  case FDCAN_DLC_BYTES_48:
    return 48U;
  case FDCAN_DLC_BYTES_64:
    return 64U;
  default:
    return 0U;
  }
}

/**
 * Submit one padded classic eight-byte service-0x22 request when initialized and TX space
 * exists.
 *
 * pid becomes big-endian bytes 2 and 3 after PCI 0x03 and service 0x22; remaining bytes
 * are zero padding. TX uses standard ID 0x7E3, eight-byte classic data format and no BRS.
 * Return false before enqueue if uninitialized or FIFO full, or after a HAL enqueue
 * failure. Success only enqueues; polling records pending PID/time afterward.
 */
static bool app_can_send_pid_request(uint16_t pid) {
  FDCAN_TxHeaderTypeDef tx_header = {0};
  uint8_t tx_data[8] = {0};

  if (!app_can_initialized) {
    return false;
  }

  if (HAL_FDCAN_GetTxFifoFreeLevel(&app_hfdcan) == 0U) {
    return false;
  }

  tx_header.Identifier = APP_CAN_ORION_REQUEST_ID;
  tx_header.IdType = FDCAN_STANDARD_ID;
  tx_header.TxFrameType = FDCAN_DATA_FRAME;
  tx_header.DataLength = FDCAN_DLC_BYTES_8;
  tx_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  tx_header.BitRateSwitch = FDCAN_BRS_OFF;
  tx_header.FDFormat = FDCAN_CLASSIC_CAN;
  tx_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  tx_header.MessageMarker = 0U;

  tx_data[0] = 0x03U;
  tx_data[1] = APP_CAN_OBD2_MODE_READ_DATA_BY_IDENTIFIER;
  tx_data[2] = (uint8_t)(pid >> 8U);
  tx_data[3] = (uint8_t)(pid & 0xFFU);

  return HAL_FDCAN_AddMessageToTxFifoQ(&app_hfdcan, &tx_header, tx_data) == HAL_OK;
}

/**
 * Accept only the table's exact ISO-TP single-frame length; larger PCI values include
 * other frame types.
 *
 * pid selects a table entry and pci_length is the complete first payload byte. Equality
 * with 4 or 5 enforces the implemented single-frame form and excludes multi-frame PCI
 * values. Unknown PID returns false; no data or scheduler state changes.
 */
static bool app_can_response_length_is_valid(uint16_t pid, uint8_t pci_length) {
  for (uint32_t index = 0U; index < APP_CAN_REQUEST_COUNT; index++) {
    if (app_can_request_table[index].pid == pid) {
      return pci_length == app_can_request_table[index].expected_obd_length;
    }
  }

  return false;
}

/**
 * Validate standard classic data-frame metadata and exact single-frame length, then decode
 * a known PID.
 *
 * Require nonnull pointers, standard classic data ID 0x7EB, exactly eight payload bytes,
 * service 0x62 and the known PID-specific PCI. Reconstruct 16-bit values big-endian;
 * summed voltage rounds raw hundredths of volts into decivolts, current subtracts 32767,
 * SOC halves/rounds/clamps, temperatures cast to signed Celsius, and cell values retain
 * 100-microvolt units. A valid current reply also mutates module statistics. Other
 * failures return false without updating the supplied snapshot. Matching a pending request
 * is not required; fault_flags is not decoded.
 */
static bool app_can_decode_orion_response(const FDCAN_RxHeaderTypeDef *rx_header,
                                          const uint8_t *rx_data,
                                          app_can_dashboard_data_t *data) {
  uint16_t pid = 0U;
  uint16_t raw_value = 0U;

  if ((rx_header == NULL) || (rx_data == NULL) || (data == NULL)) {
    return false;
  }

  if ((rx_header->Identifier != APP_CAN_ORION_RESPONSE_ID) ||
      (rx_header->IdType != FDCAN_STANDARD_ID) || (rx_header->RxFrameType != FDCAN_DATA_FRAME) ||
      (rx_header->FDFormat != FDCAN_CLASSIC_CAN)) {
    return false;
  }

  if (app_can_dlc_to_bytes(rx_header->DataLength) != 8U) {
    return false;
  }

  if (rx_data[1] != APP_CAN_OBD2_MODE_READ_DATA_RESPONSE) {
    return false;
  }

  pid = ((uint16_t)rx_data[2] << 8U) | (uint16_t)rx_data[3];
  if (!app_can_response_length_is_valid(pid, rx_data[0])) {
    return false;
  }

  unsigned signal;
  for (signal = 0; signal < APP_CAN_REQUEST_COUNT; ++signal)
    if (app_can_request_table[signal].pid == pid)
      break;
  if (signal == APP_CAN_REQUEST_COUNT)
    return false;
  data->valid_mask |= (uint16_t)(1U << signal);
  data->signal_tick_ms[signal] = HAL_GetTick();

  switch (pid) {
  case APP_CAN_PID_PACK_VOLTAGE:
    raw_value = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    data->pack_voltage_deci_volts = raw_value;
    return true;

  case APP_CAN_PID_PACK_SUMMED_VOLTAGE:
    raw_value = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    data->pack_summed_voltage_deci_volts = (uint16_t)((raw_value + 5U) / 10U);
    return true;

  case APP_CAN_PID_PACK_CURRENT:
    raw_value = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    data->pack_current_deci_amps = app_can_decode_orion_current_deci_amps(raw_value);
    app_can_update_current_stats(data);
    app_can_update_power_stats(data);
    return true;

  case APP_CAN_PID_PACK_SOC:
    data->pack_state_of_charge = app_can_clamp_percentage((uint8_t)((rx_data[4] + 1U) / 2U));
    return true;

  case APP_CAN_PID_HIGHEST_TEMPERATURE:
    data->highest_temperature_c = (int8_t)rx_data[4];
    return true;

  case APP_CAN_PID_LOWEST_TEMPERATURE:
    data->lowest_temperature_c = (int8_t)rx_data[4];
    return true;

  case APP_CAN_PID_AVERAGE_TEMPERATURE:
    data->average_temperature_c = (int8_t)rx_data[4];
    return true;

  case APP_CAN_PID_LOWEST_CELL_VOLTAGE:
    data->lowest_cell_voltage_100uv = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    return true;

  case APP_CAN_PID_HIGHEST_CELL_VOLTAGE:
    data->highest_cell_voltage_100uv = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    return true;

  case APP_CAN_PID_AVERAGE_CELL_VOLTAGE:
    data->average_cell_voltage_100uv = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    return true;

  default:
    return false;
  }
}

/**
 * Publish a successfully validated reply to the latest dashboard snapshot and optionally
 * log it.
 *
 * Start from a copy of the retained snapshot so fields outside this PID survive. A
 * successful decoder call publishes the whole updated copy and prints it; false leaves the
 * retained snapshot untouched. It does not compare the reply PID to pending state; polling
 * performs that separate sequence operation.
 */
static bool app_can_handle_rx_message(const FDCAN_RxHeaderTypeDef *rx_header,
                                      const uint8_t *rx_data) {
  app_can_dashboard_data_t next_data = app_can_latest_data;
  uint16_t pid = 0U;

  if (!app_can_decode_orion_response(rx_header, rx_data, &next_data)) {
    return false;
  }

  pid = ((uint16_t)rx_data[2] << 8U) | (uint16_t)rx_data[3];
  app_can_latest_data = next_data;
  app_can_debug_log_response(pid, rx_data, &app_can_latest_data);
  return true;
}

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
bool app_can_init(void) {
  const app_can_config_t *config = app_can_config_get();
  FDCAN_FilterTypeDef filter = {0};
  RCC_PeriphCLKInitTypeDef periph_clk = {0};

  if (app_can_initialized) {
    return true;
  }

  if (config == NULL) {
    return app_can_log_init_failure("config-null");
  }

  if (!app_can_config_is_supported(config)) {
    return app_can_log_init_failure("config-unsupported");
  }

  periph_clk.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
  periph_clk.FdcanClockSelection = RCC_FDCANCLKSOURCE_HSE;
  if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK) {
    return app_can_log_init_failure("fdcan-clock");
  }

  app_hfdcan.Instance = config->instance;
  app_hfdcan.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  app_hfdcan.Init.Mode = FDCAN_MODE_NORMAL;
  app_hfdcan.Init.AutoRetransmission = ENABLE;
  app_hfdcan.Init.TransmitPause = DISABLE;
  app_hfdcan.Init.ProtocolException = ENABLE;
  app_hfdcan.Init.NominalPrescaler = 5U;
  app_hfdcan.Init.NominalSyncJumpWidth = 1U;
  app_hfdcan.Init.NominalTimeSeg1 = 8U;
  app_hfdcan.Init.NominalTimeSeg2 = 1U;
  app_hfdcan.Init.DataPrescaler = 1U;
  app_hfdcan.Init.DataSyncJumpWidth = 1U;
  app_hfdcan.Init.DataTimeSeg1 = 1U;
  app_hfdcan.Init.DataTimeSeg2 = 1U;
  app_hfdcan.Init.MessageRAMOffset = 0U;
  app_hfdcan.Init.StdFiltersNbr = 1U;
  app_hfdcan.Init.ExtFiltersNbr = 0U;
  app_hfdcan.Init.RxFifo0ElmtsNbr = 8U;
  app_hfdcan.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
  app_hfdcan.Init.RxFifo1ElmtsNbr = 0U;
  app_hfdcan.Init.RxFifo1ElmtSize = FDCAN_DATA_BYTES_8;
  app_hfdcan.Init.RxBuffersNbr = 0U;
  app_hfdcan.Init.RxBufferSize = FDCAN_DATA_BYTES_8;
  app_hfdcan.Init.TxEventsNbr = 0U;
  app_hfdcan.Init.TxBuffersNbr = 0U;
  app_hfdcan.Init.TxFifoQueueElmtsNbr = 1U;
  app_hfdcan.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  app_hfdcan.Init.TxElmtSize = FDCAN_DATA_BYTES_8;

  if (HAL_FDCAN_Init(&app_hfdcan) != HAL_OK) {
    return app_can_log_init_failure("hal-fdcan-init");
  }

  filter.IdType = FDCAN_STANDARD_ID;
  filter.FilterIndex = 0U;
  filter.FilterType = FDCAN_FILTER_MASK;
  filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  filter.FilterID1 = APP_CAN_ORION_RESPONSE_ID;
  filter.FilterID2 = 0x7FFU;

  if (HAL_FDCAN_ConfigFilter(&app_hfdcan, &filter) != HAL_OK) {
    return app_can_log_init_failure("config-filter");
  }

  if (HAL_FDCAN_ConfigGlobalFilter(
          &app_hfdcan, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) !=
      HAL_OK) {
    return app_can_log_init_failure("global-filter");
  }

  if (HAL_FDCAN_Start(&app_hfdcan) != HAL_OK) {
    return app_can_log_init_failure("fdcan-start");
  }

  app_can_initialized = true;
  app_can_pending_pid = 0U;
  app_can_last_request_ms = 0U;
  app_can_next_request = APP_CAN_REQUEST_PACK_VOLTAGE;
  if (app_can_debug_enabled) {
    printf("[can] init ok fdcan1 request=0x%03X response=0x%03X bitrate=%lu\r\n",
           APP_CAN_ORION_REQUEST_ID,
           APP_CAN_ORION_RESPONSE_ID,
           500000UL);
  }
  return true;
}

/**
 * Expire overdue requests, drain valid replies, and send the next PID every 50 ms when
 * idle. Always copy the retained snapshot on initialized polls; unsigned elapsed-time
 * arithmetic supports tick rollover.
 *
 * data is caller storage for a full snapshot. Return true only after an accepted reply. Capture HAL
 * milliseconds once; expire pending requests only when unsigned elapsed time is strictly greater
 * than 100 ms, then drain FIFO0 until empty or HAL read failure. Valid unsolicited/duplicate
 * replies still update measurements, while only a matching PID clears pending and advances. When
 * idle and elapsed time is at least 50 ms, enqueue the next request and record its time on success.
 * Return false for null/uninitialized input or no accepted reply; consumers evaluate per-signal
 * age.
 */
bool app_can_poll_dashboard_data(app_can_dashboard_data_t *data) {
  FDCAN_RxHeaderTypeDef rx_header = {0};
  uint8_t rx_data[APP_CAN_MAX_RX_PAYLOAD_BYTES] = {0};
  bool updated = false;
  uint32_t now = HAL_GetTick();
  uint16_t request_pid = 0U;

  if ((data == NULL) || !app_can_initialized) {
    return false;
  }

  if ((app_can_pending_pid != 0U) &&
      ((now - app_can_last_request_ms) > APP_CAN_REQUEST_TIMEOUT_MS)) {
    app_can_latest_data.request_timeouts++;
    app_can_debug_log_timeout(app_can_pending_pid);
    app_can_pending_pid = 0U;
    app_can_advance_request_sequence();
  }

  while (HAL_FDCAN_GetRxFifoFillLevel(&app_hfdcan, FDCAN_RX_FIFO0) != 0U) {
    if (HAL_FDCAN_GetRxMessage(&app_hfdcan, FDCAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK) {
      break;
    }

    if (app_can_handle_rx_message(&rx_header, rx_data)) {
      updated = true;
      request_pid = ((uint16_t)rx_data[2] << 8U) | (uint16_t)rx_data[3];
      if (request_pid == app_can_pending_pid) {
        app_can_pending_pid = 0U;
        app_can_advance_request_sequence();
      }
    } else {
      app_can_latest_data.rejected_frames++;
    }
  }

  if ((app_can_pending_pid == 0U) &&
      ((now - app_can_last_request_ms) >= APP_CAN_REQUEST_PERIOD_MS)) {
    request_pid = app_can_get_next_request_pid();
    if (app_can_send_pid_request(request_pid)) {
      app_can_debug_log_request(request_pid);
      app_can_pending_pid = request_pid;
      app_can_last_request_ms = now;
    }
  }

  *data = app_can_latest_data;
  return updated;
}

/**
 * Configure clocks and RX/TX alternate-function pins only for the configured FDCAN
 * instance.
 *
 * The HAL callback handle must match the immutable configured instance. Unsupported/null
 * inputs return silently; supported port clocks are enabled before AF push-pull, no-pull,
 * very-high-speed RX and TX pins are configured independently. FDCAN1 additionally enables
 * its RCC clock. This function does not configure bus timing or test physical routing.
 */
void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef *hfdcan) {
  GPIO_InitTypeDef gpio = {0};
  const app_can_config_t *config = app_can_config_get();

  if ((hfdcan == NULL) || (config == NULL) || (hfdcan->Instance != config->instance)) {
    return;
  }

  if (!app_can_enable_gpio_clock(config->rx_pin.port) ||
      !app_can_enable_gpio_clock(config->tx_pin.port)) {
    return;
  }

  if (config->instance == FDCAN1) {
    __HAL_RCC_FDCAN_CLK_ENABLE();
  }

  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = (uint8_t)config->rx_pin.alternate_function;
  gpio.Pin = config->rx_pin.pin;
  HAL_GPIO_Init(config->rx_pin.port, &gpio);

  gpio.Alternate = (uint8_t)config->tx_pin.alternate_function;
  gpio.Pin = config->tx_pin.pin;
  HAL_GPIO_Init(config->tx_pin.port, &gpio);
}

/**
 * Release RX/TX pins and the FDCAN1 clock only when the handle matches the board
 * configuration.
 *
 * For a matching nonnull handle/configuration, deinitialize both configured GPIO pins,
 * then disable the clock for FDCAN1. Other handles return without changes. It does not
 * clear app_can_initialized or retained measurements and is a HAL resource callback, not
 * the application shutdown API.
 */
void HAL_FDCAN_MspDeInit(FDCAN_HandleTypeDef *hfdcan) {
  const app_can_config_t *config = app_can_config_get();

  if ((hfdcan == NULL) || (config == NULL) || (hfdcan->Instance != config->instance)) {
    return;
  }

  HAL_GPIO_DeInit(config->rx_pin.port, config->rx_pin.pin);
  HAL_GPIO_DeInit(config->tx_pin.port, config->tx_pin.pin);

  if (config->instance == FDCAN1) {
    __HAL_RCC_FDCAN_CLK_DISABLE();
  }
}
