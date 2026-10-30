/* Purpose: exercise the actual production CAN driver with an in-memory HAL boundary.
 * Includes production C to access private decoder helpers without duplicating their logic.
 * The fake follows the checked-in HAL's DLC-dependent copy, including malformed 64-byte RX. */
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "stm32h7xx_hal.h"
#include "../../Src/app_can_config.c"
#include "../../Src/app_can.c"

GPIO_TypeDef fake_gpio_ports[9];
FDCAN_GlobalTypeDef fake_fdcan_instance;
typedef struct {
  FDCAN_RxHeaderTypeDef header;
  uint8_t payload[64];
} fake_frame_t;
static fake_frame_t fake_rx[128];
static unsigned fake_rx_read, fake_rx_write, fake_tx_count, fake_rx_calls;
static uint32_t fake_tick, fake_tx_free;
static bool fake_rx_error, fake_tx_error;
static unsigned fake_init_failure, fake_gpio_init_count, fake_gpio_deinit_count;
static FDCAN_TxHeaderTypeDef fake_last_tx_header;
static uint8_t fake_last_tx_payload[8];
static FDCAN_FilterTypeDef fake_filter;
static uint32_t fake_global_filter[4];
static unsigned assertions;
#define CHECK(condition)                                                                           \
  do {                                                                                             \
    assertions++;                                                                                  \
    if (!(condition)) {                                                                            \
      fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #condition);                         \
      return false;                                                                                \
    }                                                                                              \
  } while (0)

/**
 * Return the test-controlled unsigned millisecond clock without wall-clock delays.
 *
 * Inputs: none. Returns the fake uint32 millisecond counter without mutation; tests move
 * it directly to precise request/timeout/wrap boundaries.
 */
uint32_t HAL_GetTick(void) {
  return fake_tick;
}

/**
 * Expose configurable FIFO backpressure to the production transmit function.
 *
 * Input: handle ignored. Returns fake_tx_free without mutation, enabling deterministic no-
 * slot retries.
 */
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(FDCAN_HandleTypeDef *handle) {
  (void)handle;
  return fake_tx_free;
}

/**
 * Record successful outgoing headers/payloads, or inject a HAL transmission failure.
 *
 * Inputs: valid request header/payload; handle ignored. Returns HAL_ERROR with no capture
 * when fake_tx_error is set, otherwise copies eight bytes/header, increments fake_tx_count
 * and returns HAL_OK.
 */
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *handle,
                                                FDCAN_TxHeaderTypeDef *header,
                                                uint8_t *payload) {
  (void)handle;
  if (fake_tx_error) {
    return HAL_ERROR;
  }
  fake_last_tx_header = *header;
  memcpy(fake_last_tx_payload, payload, sizeof(fake_last_tx_payload));
  fake_tx_count++;
  return HAL_OK;
}

/**
 * Report the number of queued frames, independent of metadata validation.
 *
 * Inputs: handle/fifo ignored. Returns fake_rx_write-fake_rx_read without consuming
 * entries; deliberately bypasses acceptance filtering so decoder rejection remains
 * observable.
 */
uint32_t HAL_FDCAN_GetRxFifoFillLevel(FDCAN_HandleTypeDef *handle, uint32_t fifo) {
  (void)handle;
  (void)fifo;
  return fake_rx_write - fake_rx_read;
}

/**
 * Copy DLC bytes exactly as the local HAL does; preserve the FIFO on an injected error.
 *
 * Inputs: writable header/payload, ignored handle/fifo. Counts every attempted read;
 * error/empty returns HAL_ERROR without consumption. Success asserts valid DLC code,
 * copies its full byte count (up to 64), advances fake_rx_read and returns HAL_OK,
 * reproducing the HAL buffer-size boundary.
 */
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *handle,
                                         uint32_t fifo,
                                         FDCAN_RxHeaderTypeDef *header,
                                         uint8_t *payload) {
  static const uint8_t lengths[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
  (void)handle;
  (void)fifo;
  fake_rx_calls++;
  if (fake_rx_error || fake_rx_read == fake_rx_write) {
    return HAL_ERROR;
  }
  *header = fake_rx[fake_rx_read].header;
  assert(header->DataLength < 16U);
  memcpy(payload, fake_rx[fake_rx_read].payload, lengths[header->DataLength]);
  fake_rx_read++;
  return HAL_OK;
}

/**
 * Inject failure at the FDCAN peripheral-clock configuration boundary.
 *
 * Input: clock ignored. Returns HAL_ERROR for injected initialization stage 1, otherwise
 * HAL_OK; touches no RCC registers.
 */
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *clock) {
  (void)clock;
  return fake_init_failure == 1U ? HAL_ERROR : HAL_OK;
}

/**
 * Inject failure at FDCAN initialization without accessing memory-mapped registers.
 *
 * Input: handle ignored. Returns HAL_ERROR for initialization stage 2, otherwise HAL_OK;
 * no controller state is emulated.
 */
HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef *handle) {
  (void)handle;
  return fake_init_failure == 2U ? HAL_ERROR : HAL_OK;
}

/**
 * Capture the configured standard-ID mask and optionally reject its initialization.
 *
 * Inputs: valid filter and ignored handle. Captures the filter before returning stage-3
 * HAL_ERROR or HAL_OK, allowing exact-ID/mask assertions independently of success.
 */
HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *handle, FDCAN_FilterTypeDef *filter) {
  (void)handle;
  fake_filter = *filter;
  return fake_init_failure == 3U ? HAL_ERROR : HAL_OK;
}

/**
 * Capture rejection policies for nonmatching and remote frames, with fault injection.
 *
 * Inputs: four acceptance policy constants and ignored handle. Stores each policy then
 * returns stage-4 HAL_ERROR or HAL_OK; actual FIFO admission remains controlled by
 * enqueue.
 */
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *handle,
                                               uint32_t standard,
                                               uint32_t extended,
                                               uint32_t remote_standard,
                                               uint32_t remote_extended) {
  (void)handle;
  fake_global_filter[0] = standard;
  fake_global_filter[1] = extended;
  fake_global_filter[2] = remote_standard;
  fake_global_filter[3] = remote_extended;
  return fake_init_failure == 4U ? HAL_ERROR : HAL_OK;
}

/**
 * Inject controller-start failure while performing no hardware operation.
 *
 * Input: handle ignored. Returns stage-5 HAL_ERROR or HAL_OK; no hardware transmission is
 * enabled.
 */
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *handle) {
  (void)handle;
  return fake_init_failure == 5U ? HAL_ERROR : HAL_OK;
}

/**
 * Count GPIO initialization calls so MSP guard paths can be checked on the host.
 *
 * Inputs: port/gpio ignored. Returns nothing; increments fake_gpio_init_count to prove MSP
 * guards and expected call counts without validating electrical pin behavior.
 */
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *gpio) {
  (void)port;
  (void)gpio;
  fake_gpio_init_count++;
}

/**
 * Count GPIO deinitialization calls without altering host state outside this harness.
 *
 * Inputs: port/pin ignored. Returns nothing; increments fake_gpio_deinit_count to observe
 * MSP teardown calls.
 */
void HAL_GPIO_DeInit(GPIO_TypeDef *port, uint32_t pin) {
  (void)port;
  (void)pin;
  fake_gpio_deinit_count++;
}

/**
 * Reset every driver singleton and fake boundary so tests run independently.
 *
 * Inputs: none. Returns nothing; clears all production CAN scheduling/statistics/telemetry
 * singletons and fake FIFO/TX/error/filter state, restores one TX slot and disables debug
 * printing. Each independent group starts from this reset rather than inheriting earlier
 * measurements.
 */
static void reset_state(void) {
  app_hfdcan = (FDCAN_HandleTypeDef){0};
  app_can_initialized = false;
  app_can_latest_data = (app_can_dashboard_data_t){0};
  app_can_next_request = APP_CAN_REQUEST_PACK_VOLTAGE;
  app_can_pending_pid = 0U;
  app_can_last_request_ms = 0U;
  app_can_debug_enabled = false;
  app_can_current_stats_valid = false;
  app_can_current_min_deci_amps = app_can_current_max_deci_amps = 0;
  app_can_current_sum_deci_amps = 0;
  app_can_current_sample_count = 0;
  app_can_power_stats_valid = false;
  app_can_power_min_kw = app_can_power_max_kw = 0;
  app_can_power_sum_kw = 0;
  app_can_power_sample_count = 0;
  fake_rx_read = fake_rx_write = fake_tx_count = fake_rx_calls = 0U;
  fake_tick = 0U;
  fake_tx_free = 1U;
  fake_rx_error = fake_tx_error = false;
  fake_init_failure = fake_gpio_init_count = fake_gpio_deinit_count = 0U;
  memset(&fake_last_tx_header, 0, sizeof(fake_last_tx_header));
  memset(fake_last_tx_payload, 0, sizeof(fake_last_tx_payload));
  memset(&fake_filter, 0, sizeof(fake_filter));
  memset(fake_global_filter, 0, sizeof(fake_global_filter));
}

/**
 * Create a standard classic eight-byte response according to the implemented PID table.
 *
 * Inputs: pid is a 16-bit implemented or deliberately unknown PID; value is raw protocol
 * data, not an engineering-unit float. Returns a zero-padded classic eight-byte frame with
 * metadata, service and table-selected PCI length. One-byte fields use the low value byte;
 * two-byte fields use big-endian order. It changes no driver state.
 */
static fake_frame_t response(uint16_t pid, uint16_t value) {
  fake_frame_t frame = {0};
  frame.header.Identifier = APP_CAN_ORION_RESPONSE_ID;
  frame.header.IdType = FDCAN_STANDARD_ID;
  frame.header.RxFrameType = FDCAN_DATA_FRAME;
  frame.header.FDFormat = FDCAN_CLASSIC_CAN;
  frame.header.DataLength = FDCAN_DLC_BYTES_8;
  frame.payload[0] = 5U;
  for (unsigned i = 0; i < APP_CAN_REQUEST_COUNT; i++) {
    if (app_can_request_table[i].pid == pid) {
      frame.payload[0] = app_can_request_table[i].expected_obd_length;
    }
  }
  frame.payload[1] = APP_CAN_OBD2_MODE_READ_DATA_RESPONSE;
  frame.payload[2] = (uint8_t)(pid >> 8U);
  frame.payload[3] = (uint8_t)pid;
  if (frame.payload[0] == 4U) {
    frame.payload[4] = (uint8_t)value;
  } else {
    frame.payload[4] = (uint8_t)(value >> 8U);
    frame.payload[5] = (uint8_t)value;
  }
  return frame;
}

/**
 * Append an RX frame to the bounded fake FIFO; test queue overflow is a harness error.
 *
 * Input: frame is copied by value into the fake RX batch. Returns nothing; queue overflow
 * aborts as a harness construction error rather than silently discarding test frames.
 */
static void enqueue(fake_frame_t frame) {
  assert(fake_rx_write < sizeof(fake_rx) / sizeof(fake_rx[0]));
  fake_rx[fake_rx_write++] = frame;
}

/**
 * Decode through the production response decoder, leaving no duplicate conversion code.
 *
 * Inputs: pid/value form a synthetic valid response and data is the decoder destination.
 * Returns the actual private production decoder's boolean, including its data/statistics
 * mutations; no duplicated signal conversion is used.
 */
static bool decode(uint16_t pid, uint16_t value, app_can_dashboard_data_t *data) {
  fake_frame_t frame = response(pid, value);
  return app_can_decode_orion_response(&frame.header, frame.payload, data);
}

/**
 * Prove initialization failure paths stay disabled and a later retry installs exact
 * filters.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_initialization_and_recovery(void) {
  for (unsigned stage = 1U; stage <= 5U; stage++) {
    reset_state();
    fake_init_failure = stage;
    CHECK(!app_can_init());
    CHECK(!app_can_initialized);
    fake_init_failure = 0U;
    CHECK(app_can_init());
    CHECK(app_can_init());
    CHECK(app_hfdcan.Init.FrameFormat == FDCAN_FRAME_CLASSIC);
    CHECK(fake_filter.FilterID1 == 0x7EBU && fake_filter.FilterID2 == 0x7FFU);
    CHECK(fake_global_filter[0] == FDCAN_REJECT && fake_global_filter[1] == FDCAN_REJECT);
    CHECK(fake_global_filter[2] == FDCAN_REJECT_REMOTE &&
          fake_global_filter[3] == FDCAN_REJECT_REMOTE);
  }
  return true;
}

/**
 * Verify null/public API guards, debug toggling and supported GPIO/config checks.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_null_guards_and_configuration(void) {
  reset_state();
  fake_frame_t frame = response(APP_CAN_PID_PACK_VOLTAGE, 123U);
  app_can_dashboard_data_t data = {0};
  app_can_config_t invalid = *app_can_config_get();
  invalid.mode = APP_CAN_MODE_FD;
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(!app_can_poll_dashboard_data(NULL));
  CHECK(!app_can_send_pid_request(APP_CAN_PID_PACK_VOLTAGE));
  CHECK(!app_can_decode_orion_response(NULL, frame.payload, &data));
  CHECK(!app_can_decode_orion_response(&frame.header, NULL, &data));
  CHECK(!app_can_decode_orion_response(&frame.header, frame.payload, NULL));
  CHECK(!app_can_config_is_supported(NULL));
  CHECK(!app_can_config_is_supported(&invalid));
  CHECK(app_can_config_is_supported(app_can_config_get()));
  CHECK(app_can_get_display_voltage_deci_volts(NULL) == 0U);
  app_can_update_current_stats(NULL);
  app_can_update_power_stats(NULL);
  CHECK(!app_can_current_stats_valid && !app_can_power_stats_valid);
  app_can_set_debug_enabled(true);
  CHECK(app_can_is_debug_enabled());
  app_can_set_debug_enabled(false);
  CHECK(!app_can_is_debug_enabled());
  for (unsigned i = 0; i < 9; i++) {
    CHECK(app_can_enable_gpio_clock(&fake_gpio_ports[i]));
  }
  CHECK(!app_can_enable_gpio_clock(NULL));
  HAL_FDCAN_MspInit(NULL);
  HAL_FDCAN_MspDeInit(NULL);
  CHECK(fake_gpio_init_count == 0U && fake_gpio_deinit_count == 0U);
  app_hfdcan.Instance = FDCAN1;
  HAL_FDCAN_MspInit(&app_hfdcan);
  HAL_FDCAN_MspDeInit(&app_hfdcan);
  CHECK(fake_gpio_init_count == 2U && fake_gpio_deinit_count == 2U);
  CHECK(app_can_init());
  fake_tick = 50U;
  CHECK(!app_can_poll_dashboard_data(NULL));
  CHECK(fake_tx_count == 0U);
  return true;
}

/**
 * Check every PID updates its intended field with the existing voltage/current/SOC units.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_all_pid_conversions(void) {
  reset_state();
  app_can_dashboard_data_t data = {0};
  CHECK(decode(APP_CAN_PID_PACK_VOLTAGE, 12345U, &data));
  CHECK(data.pack_voltage_deci_volts == 12345U);
  CHECK(decode(APP_CAN_PID_PACK_SUMMED_VOLTAGE, 12344U, &data));
  CHECK(data.pack_summed_voltage_deci_volts == 1234U);
  CHECK(decode(APP_CAN_PID_PACK_SUMMED_VOLTAGE, 12345U, &data));
  CHECK(data.pack_summed_voltage_deci_volts == 1235U);
  CHECK(decode(APP_CAN_PID_PACK_CURRENT, 32768U, &data));
  CHECK(data.pack_current_deci_amps == 1);
  CHECK(decode(APP_CAN_PID_PACK_SOC, 101U, &data));
  CHECK(data.pack_state_of_charge == 51U);
  CHECK(decode(APP_CAN_PID_HIGHEST_TEMPERATURE, 127U, &data));
  CHECK(data.highest_temperature_c == 127);
  CHECK(decode(APP_CAN_PID_LOWEST_TEMPERATURE, 128U, &data));
  CHECK(data.lowest_temperature_c == -128);
  CHECK(decode(APP_CAN_PID_AVERAGE_TEMPERATURE, 255U, &data));
  CHECK(data.average_temperature_c == -1);
  CHECK(decode(APP_CAN_PID_LOWEST_CELL_VOLTAGE, 0U, &data));
  CHECK(data.lowest_cell_voltage_100uv == 0U);
  CHECK(decode(APP_CAN_PID_HIGHEST_CELL_VOLTAGE, UINT16_MAX, &data));
  CHECK(data.highest_cell_voltage_100uv == UINT16_MAX);
  CHECK(decode(APP_CAN_PID_AVERAGE_CELL_VOLTAGE, 37000U, &data));
  CHECK(data.average_cell_voltage_100uv == 37000U);
  CHECK(data.fault_flags == 0U);
  return true;
}

/**
 * Distinguish an absent signal from a real zero and check the inclusive freshness limit.
 * Invalid signal indices and null snapshots are never fresh; timestamps wrap as uint32.
 */
static bool test_signal_freshness_boundaries(void) {
  app_can_dashboard_data_t data = {0};
  CHECK(!app_can_signal_is_fresh(NULL, APP_CAN_SIGNAL_PACK_VOLTAGE, 0U));
  CHECK(!app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_PACK_VOLTAGE, 0U));
  CHECK(!app_can_signal_is_fresh(&data, (app_can_signal_id_t)-1, 0U));
  CHECK(!app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_COUNT, 0U));

  data.valid_mask = (uint16_t)(1U << APP_CAN_SIGNAL_PACK_VOLTAGE);
  data.signal_tick_ms[APP_CAN_SIGNAL_PACK_VOLTAGE] = 0U;
  CHECK(app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_PACK_VOLTAGE, 0U));
  CHECK(app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_PACK_VOLTAGE, APP_CAN_STALE_MS));
  CHECK(!app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_PACK_VOLTAGE, APP_CAN_STALE_MS + 1U));
  CHECK(!app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_SOC, 0U));

  data.signal_tick_ms[APP_CAN_SIGNAL_PACK_VOLTAGE] = UINT32_MAX - 25U;
  CHECK(app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_PACK_VOLTAGE, 24U));
  CHECK(app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_PACK_VOLTAGE, APP_CAN_STALE_MS - 26U));
  CHECK(!app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_PACK_VOLTAGE, APP_CAN_STALE_MS - 25U));
  return true;
}

/**
 * Accepted replies establish validity and stamp only their own signal, including zeros.
 * The retained snapshot remains available on empty polls while its age becomes stale.
 */
static bool test_each_pid_has_independent_validity_and_age(void) {
  static const struct {
    uint16_t pid;
    uint16_t raw_zero;
    app_can_signal_id_t signal;
  } samples[] = {
      {APP_CAN_PID_PACK_VOLTAGE, 0U, APP_CAN_SIGNAL_PACK_VOLTAGE},
      {APP_CAN_PID_PACK_SUMMED_VOLTAGE, 0U, APP_CAN_SIGNAL_PACK_SUMMED_VOLTAGE},
      {APP_CAN_PID_PACK_CURRENT, 32767U, APP_CAN_SIGNAL_PACK_CURRENT},
      {APP_CAN_PID_PACK_SOC, 0U, APP_CAN_SIGNAL_SOC},
      {APP_CAN_PID_HIGHEST_TEMPERATURE, 0U, APP_CAN_SIGNAL_TEMP_HIGH},
      {APP_CAN_PID_LOWEST_TEMPERATURE, 0U, APP_CAN_SIGNAL_TEMP_LOW},
      {APP_CAN_PID_AVERAGE_TEMPERATURE, 0U, APP_CAN_SIGNAL_TEMP_AVG},
      {APP_CAN_PID_LOWEST_CELL_VOLTAGE, 0U, APP_CAN_SIGNAL_CELL_LOW},
      {APP_CAN_PID_HIGHEST_CELL_VOLTAGE, 0U, APP_CAN_SIGNAL_CELL_HIGH},
      {APP_CAN_PID_AVERAGE_CELL_VOLTAGE, 0U, APP_CAN_SIGNAL_CELL_AVG},
  };
  reset_state();
  CHECK(app_can_init());
  app_can_dashboard_data_t data = {0};
  uint16_t expected_mask = 0U;
  uint32_t expected_ticks[APP_CAN_SIGNAL_COUNT] = {0};
  for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
    fake_tick = i * 31U; /* The first valid zero arrives at timestamp zero. */
    enqueue(response(samples[i].pid, samples[i].raw_zero));
    CHECK(app_can_poll_dashboard_data(&data));
    expected_mask |= (uint16_t)(1U << samples[i].signal);
    expected_ticks[samples[i].signal] = fake_tick;
    CHECK(data.valid_mask == expected_mask);
    CHECK(app_can_signal_is_fresh(&data, samples[i].signal, fake_tick));
    for (unsigned signal = 0; signal < APP_CAN_SIGNAL_COUNT; signal++) {
      CHECK(data.signal_tick_ms[signal] == expected_ticks[signal]);
    }
  }
  CHECK(data.pack_voltage_deci_volts == 0U);
  CHECK(data.pack_current_deci_amps == 0);
  CHECK(data.pack_state_of_charge == 0U);
  CHECK(data.lowest_cell_voltage_100uv == 0U);

  fake_tick = APP_CAN_STALE_MS + 1U;
  data = (app_can_dashboard_data_t){0};
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(data.valid_mask == expected_mask);
  CHECK(!app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_PACK_VOLTAGE, fake_tick));
  CHECK(app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_SOC, fake_tick));
  CHECK(data.signal_tick_ms[APP_CAN_SIGNAL_PACK_VOLTAGE] == 0U);
  return true;
}

/**
 * Wrong ID/service/PCI/DLC/PID/frame metadata and HAL read errors never refresh a value.
 * A later legitimate reply recovers only that signal after the old sample has expired.
 */
static bool test_invalid_replies_do_not_refresh_signal_age(void) {
  reset_state();
  CHECK(app_can_init());
  app_can_dashboard_data_t data = {0};
  fake_tick = 42U;
  enqueue(response(APP_CAN_PID_PACK_VOLTAGE, 4000U));
  CHECK(app_can_poll_dashboard_data(&data));
  const uint16_t expected_mask = (uint16_t)(1U << APP_CAN_SIGNAL_PACK_VOLTAGE);
  CHECK(data.valid_mask == expected_mask);

  fake_tick += APP_CAN_STALE_MS + 1U;
  for (unsigned mutation = 0; mutation < 8U; mutation++) {
    fake_frame_t malformed = response(APP_CAN_PID_PACK_VOLTAGE, 4500U);
    switch (mutation) {
    case 0U:
      malformed.header.Identifier = 0x123U;
      break;
    case 1U:
      malformed.payload[1] = 0x7FU;
      break;
    case 2U:
      malformed.payload[0] = 4U;
      break;
    case 3U:
      malformed.header.DataLength = FDCAN_DLC_BYTES_7;
      break;
    case 4U:
      malformed = response(0xFFFFU, 4500U);
      break;
    case 5U:
      malformed.header.IdType = FDCAN_EXTENDED_ID;
      break;
    case 6U:
      malformed.header.RxFrameType = FDCAN_REMOTE_FRAME;
      break;
    default:
      malformed.header.FDFormat = FDCAN_FD_CAN;
      break;
    }
    enqueue(malformed);
    CHECK(!app_can_poll_dashboard_data(&data));
    CHECK(data.valid_mask == expected_mask);
    CHECK(data.pack_voltage_deci_volts == 4000U);
    CHECK(data.signal_tick_ms[APP_CAN_SIGNAL_PACK_VOLTAGE] == 42U);
    CHECK(!app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_PACK_VOLTAGE, fake_tick));
  }

  enqueue(response(APP_CAN_PID_PACK_VOLTAGE, 4500U));
  fake_rx_error = true;
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(data.signal_tick_ms[APP_CAN_SIGNAL_PACK_VOLTAGE] == 42U);
  fake_rx_error = false;
  CHECK(app_can_poll_dashboard_data(&data));
  CHECK(data.pack_voltage_deci_volts == 4500U);
  CHECK(data.signal_tick_ms[APP_CAN_SIGNAL_PACK_VOLTAGE] == fake_tick);
  CHECK(data.valid_mask == expected_mask);
  CHECK(app_can_signal_is_fresh(&data, APP_CAN_SIGNAL_PACK_VOLTAGE, fake_tick));
  return true;
}

/**
 * Exhaust raw current, SOC and signed-temperature domains to check boundaries and scaling.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_exhaustive_scalar_domains(void) {
  reset_state();
  app_can_dashboard_data_t data = {0};
  for (uint32_t raw = 0; raw <= UINT16_MAX; raw++) {
    int32_t expected = (int32_t)raw - 32767;
    if (expected > INT16_MAX) {
      expected = INT16_MAX;
    }
    CHECK(app_can_decode_orion_current_deci_amps((uint16_t)raw) == expected);
  }
  for (unsigned raw = 0; raw <= UINT8_MAX; raw++) {
    unsigned soc = (raw + 1U) / 2U;
    if (soc > 100U) {
      soc = 100U;
    }
    CHECK(decode(APP_CAN_PID_PACK_SOC, (uint16_t)raw, &data));
    CHECK(data.pack_state_of_charge == soc);
    CHECK(decode(APP_CAN_PID_AVERAGE_TEMPERATURE, (uint16_t)raw, &data));
    CHECK(data.average_temperature_c == (raw < 128U ? (int)raw : (int)raw - 256));
  }
  return true;
}

/**
 * Verify exact single-frame PCI length for each PID, rejecting all other byte values.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_exact_single_frame_lengths(void) {
  reset_state();
  app_can_dashboard_data_t data = {0};
  for (unsigned pid_index = 0; pid_index < APP_CAN_REQUEST_COUNT; pid_index++) {
    fake_frame_t frame = response(app_can_request_table[pid_index].pid, 100U);
    for (unsigned pci = 0; pci <= UINT8_MAX; pci++) {
      frame.payload[0] = (uint8_t)pci;
      CHECK(app_can_decode_orion_response(&frame.header, frame.payload, &data) ==
            (pci == app_can_request_table[pid_index].expected_obd_length));
    }
  }
  return true;
}

/**
 * Reject extended IDs, remote frames and CAN FD before any dashboard/statistics mutation.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_frame_metadata_rejection(void) {
  for (unsigned mutation = 0; mutation < 3; mutation++) {
    reset_state();
    fake_frame_t frame = response(APP_CAN_PID_PACK_CURRENT, 100U);
    app_can_dashboard_data_t data = {0};
    if (mutation == 0) {
      frame.header.IdType = FDCAN_EXTENDED_ID;
    }
    if (mutation == 1) {
      frame.header.RxFrameType = FDCAN_REMOTE_FRAME;
    }
    if (mutation == 2) {
      frame.header.FDFormat = FDCAN_FD_CAN;
    }
    CHECK(!app_can_decode_orion_response(&frame.header, frame.payload, &data));
    CHECK(data.pack_current_deci_amps == 0 && !app_can_current_stats_valid);
  }
  return true;
}

/**
 * Admit the eight-byte protocol contract only, including rejection of all FD DLC codes.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_dlc_bounds_and_unknown_messages(void) {
  reset_state();
  fake_frame_t frame = response(APP_CAN_PID_PACK_VOLTAGE, 10U);
  app_can_dashboard_data_t data = {0};
  for (uint32_t dlc = 0; dlc <= 16; dlc++) {
    frame.header.DataLength = dlc;
    CHECK(app_can_decode_orion_response(&frame.header, frame.payload, &data) == (dlc == 8U));
  }
  frame.header.DataLength = FDCAN_DLC_BYTES_8;
  frame.header.Identifier++;
  CHECK(!app_can_decode_orion_response(&frame.header, frame.payload, &data));
  frame.header.Identifier = APP_CAN_ORION_RESPONSE_ID;
  frame.payload[1] = 0x7FU;
  CHECK(!app_can_decode_orion_response(&frame.header, frame.payload, &data));
  frame = response(0xFFFFU, 10U);
  CHECK(!app_can_decode_orion_response(&frame.header, frame.payload, &data));
  return true;
}

/**
 * Confirm signed nearest-integer power rounding, voltage fallback and integer saturation.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_power_rounding_and_voltage_fallback(void) {
  reset_state();
  app_can_dashboard_data_t data = {0};
  CHECK(app_can_compute_power_kw(49, 1000U) == 0);
  CHECK(app_can_compute_power_kw(50, 1000U) == 1);
  CHECK(app_can_compute_power_kw(-49, 1000U) == 0);
  CHECK(app_can_compute_power_kw(-50, 1000U) == -1);
  CHECK(app_can_compute_power_kw(INT16_MAX, UINT16_MAX) == 21474);
  CHECK(app_can_compute_power_kw(INT16_MIN, UINT16_MAX) == -21475);
  CHECK(app_can_clamp_int64_to_int16(INT64_MAX) == INT16_MAX);
  CHECK(app_can_clamp_int64_to_int16(INT64_MIN) == INT16_MIN);
  data.pack_summed_voltage_deci_volts = 4000U;
  CHECK(app_can_get_display_voltage_deci_volts(&data) == 4000U);
  data.pack_voltage_deci_volts = 4100U;
  CHECK(app_can_get_display_voltage_deci_volts(&data) == 4100U);
  return true;
}

/**
 * Check min/mean/max updates and power omission until voltage exists, including negative
 * ties.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_current_and_power_statistics(void) {
  reset_state();
  app_can_dashboard_data_t data = {0};
  CHECK(decode(APP_CAN_PID_PACK_CURRENT, 32777U, &data));
  CHECK(data.average_pack_current_deci_amps == 10 && !app_can_power_stats_valid);
  CHECK(decode(APP_CAN_PID_PACK_VOLTAGE, 1000U, &data));
  CHECK(decode(APP_CAN_PID_PACK_CURRENT, 32817U, &data));
  CHECK(data.lowest_pack_current_deci_amps == 10);
  CHECK(data.average_pack_current_deci_amps == 30 && data.highest_pack_current_deci_amps == 50);
  CHECK(data.lowest_pack_power_kw == 1 && data.average_pack_power_kw == 1);
  CHECK(decode(APP_CAN_PID_PACK_CURRENT, 32717U, &data));
  CHECK(data.lowest_pack_current_deci_amps == -50 && data.average_pack_current_deci_amps == 3);
  CHECK(data.highest_pack_current_deci_amps == 50);
  CHECK(data.lowest_pack_power_kw == -1 && data.average_pack_power_kw == 0);
  CHECK(data.highest_pack_power_kw == 1);
  reset_state();
  data = (app_can_dashboard_data_t){0};
  CHECK(decode(APP_CAN_PID_PACK_CURRENT, 32766U, &data));
  CHECK(decode(APP_CAN_PID_PACK_CURRENT, 32765U, &data));
  CHECK(data.average_pack_current_deci_amps == -2);
  return true;
}

/**
 * Drive responses for all ten PIDs and verify request bytes, period and wrap to the first
 * PID.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_request_cycle_and_payload(void) {
  reset_state();
  CHECK(app_can_init());
  app_can_dashboard_data_t data = {0};
  fake_tick = 49U;
  CHECK(!app_can_poll_dashboard_data(&data) && fake_tx_count == 0U);
  for (unsigned i = 0; i <= APP_CAN_REQUEST_COUNT; i++) {
    if (i > 0) {
      enqueue(response(app_can_request_table[i - 1].pid, 100U));
    }
    fake_tick = (i + 1U) * 50U;
    CHECK(app_can_poll_dashboard_data(&data) == (i > 0));
    uint16_t expected = app_can_request_table[i % APP_CAN_REQUEST_COUNT].pid;
    CHECK(fake_tx_count == i + 1U);
    CHECK(fake_last_tx_header.Identifier == 0x7E3U);
    CHECK(fake_last_tx_header.IdType == FDCAN_STANDARD_ID);
    CHECK(fake_last_tx_header.TxFrameType == FDCAN_DATA_FRAME);
    CHECK(fake_last_tx_header.FDFormat == FDCAN_CLASSIC_CAN);
    CHECK(fake_last_tx_header.DataLength == FDCAN_DLC_BYTES_8);
    CHECK(fake_last_tx_payload[0] == 3U && fake_last_tx_payload[1] == 0x22U);
    CHECK(fake_last_tx_payload[2] == (expected >> 8U));
    CHECK(fake_last_tx_payload[3] == (uint8_t)expected);
    CHECK(fake_last_tx_payload[4] == 0U && fake_last_tx_payload[7] == 0U);
    CHECK(app_can_pending_pid == expected);
  }
  return true;
}

/**
 * Verify strict greater-than timeout boundary and progression when a response is absent.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_timeout_boundary(void) {
  reset_state();
  CHECK(app_can_init());
  app_can_dashboard_data_t data = {0};
  fake_tick = 50U;
  CHECK(!app_can_poll_dashboard_data(&data));
  fake_tick = 150U;
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(fake_tx_count == 1U && app_can_pending_pid == APP_CAN_PID_PACK_VOLTAGE);
  fake_tick = 151U;
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(fake_tx_count == 2U && app_can_pending_pid == APP_CAN_PID_PACK_SUMMED_VOLTAGE);
  return true;
}

/**
 * Exercise elapsed-time arithmetic across uint32 millisecond rollover without sleeps.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_tick_rollover(void) {
  reset_state();
  CHECK(app_can_init());
  app_can_dashboard_data_t data = {0};
  fake_tick = UINT32_MAX - 20U;
  CHECK(!app_can_poll_dashboard_data(&data));
  fake_tick = 79U;
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(fake_tx_count == 1U);
  fake_tick = 80U;
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(fake_tx_count == 2U && app_can_pending_pid == APP_CAN_PID_PACK_SUMMED_VOLTAGE);
  return true;
}

/**
 * Prove TX backpressure and HAL errors leave the unsent request available for retry.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_tx_backpressure_and_recovery(void) {
  reset_state();
  CHECK(app_can_init());
  app_can_dashboard_data_t data = {0};
  fake_tick = 50U;
  fake_tx_free = 0U;
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(fake_tx_count == 0U && app_can_pending_pid == 0U && app_can_last_request_ms == 0U);
  fake_tx_free = 1U;
  fake_tx_error = true;
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(fake_tx_count == 0U && app_can_pending_pid == 0U);
  fake_tx_error = false;
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(fake_tx_count == 1U && app_can_pending_pid == APP_CAN_PID_PACK_VOLTAGE);
  return true;
}

/**
 * Demonstrate RX errors terminate a poll, preserve queued data and allow a later
 * successful drain.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_rx_failure_fifo_burst_and_recovery(void) {
  reset_state();
  CHECK(app_can_init());
  app_can_dashboard_data_t data = {0};
  for (unsigned i = 0; i < 64U; i++) {
    enqueue(response(APP_CAN_PID_PACK_VOLTAGE, (uint16_t)i));
  }
  fake_rx_error = true;
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(fake_rx_calls == 1U && fake_rx_read == 0U);
  fake_rx_error = false;
  CHECK(app_can_poll_dashboard_data(&data));
  CHECK(fake_rx_read == 64U && data.pack_voltage_deci_volts == 63U);
  return true;
}

/**
 * Valid unsolicited responses update telemetry but only the outstanding PID advances
 * requests.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_unsolicited_and_malformed_pending_responses(void) {
  reset_state();
  CHECK(app_can_init());
  app_can_dashboard_data_t data = {0};
  fake_tick = 50U;
  CHECK(!app_can_poll_dashboard_data(&data));
  enqueue(response(APP_CAN_PID_PACK_SOC, 100U));
  CHECK(app_can_poll_dashboard_data(&data));
  CHECK(data.pack_state_of_charge == 50U && app_can_pending_pid == APP_CAN_PID_PACK_VOLTAGE);
  fake_frame_t malformed = response(APP_CAN_PID_PACK_VOLTAGE, 4444U);
  malformed.payload[0] = 0x15U;
  enqueue(malformed);
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(data.pack_voltage_deci_volts == 0U && app_can_pending_pid == APP_CAN_PID_PACK_VOLTAGE);
  enqueue(response(APP_CAN_PID_PACK_VOLTAGE, 4000U));
  CHECK(app_can_poll_dashboard_data(&data));
  CHECK(data.pack_voltage_deci_volts == 4000U && app_can_pending_pid == 0U);
  CHECK(fake_tx_count == 1U);
  fake_tick = 100U;
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(app_can_pending_pid == APP_CAN_PID_PACK_SUMMED_VOLTAGE);
  return true;
}

/**
 * Reproduce HAL's 64-byte copy into the driver's RX buffer, then require rejection and
 * recovery.
 *
 * Inputs: none; this independent group resets driver/fake state before exercising the
 * cases described above. Returns true when every CHECK succeeds, or false immediately
 * after CHECK records and prints the failing expression. It uses explicit fake
 * frames/clocks/error flags, not wall-clock delays or physical CAN traffic.
 */
static bool test_oversized_hal_payload(void) {
  reset_state();
  CHECK(app_can_init());
  fake_frame_t oversized = response(APP_CAN_PID_PACK_VOLTAGE, 4444U);
  oversized.header.DataLength = FDCAN_DLC_BYTES_64;
  oversized.header.FDFormat = FDCAN_FD_CAN;
  enqueue(oversized);
  app_can_dashboard_data_t data = {0};
  CHECK(!app_can_poll_dashboard_data(&data));
  CHECK(data.pack_voltage_deci_volts == 0U);
  enqueue(response(APP_CAN_PID_PACK_VOLTAGE, 4000U));
  CHECK(app_can_poll_dashboard_data(&data));
  CHECK(data.pack_voltage_deci_volts == 4000U);
  return true;
}

/**
 * Serialize the retained production snapshot after each virtual input, including rejection
 * status.
 *
 * Input: updated is the latest production poll result. Returns nothing; reads the retained
 * driver snapshot, prints named signed/unsigned integer fields in their production units
 * and flushes stdout. It does not consume FIFO/TX or change telemetry.
 */
static void print_dashboard_json(bool updated) {
  const app_can_dashboard_data_t *data = &app_can_latest_data;
  printf("{\"updated\":%s", updated ? "true" : "false");
#define PRINT_UNSIGNED(field) printf(",\"" #field "\":%u", (unsigned int)data->field)
#define PRINT_SIGNED(field) printf(",\"" #field "\":%d", (int)data->field)
  PRINT_UNSIGNED(pack_voltage_deci_volts);
  PRINT_UNSIGNED(pack_summed_voltage_deci_volts);
  PRINT_UNSIGNED(lowest_cell_voltage_100uv);
  PRINT_UNSIGNED(average_cell_voltage_100uv);
  PRINT_UNSIGNED(highest_cell_voltage_100uv);
  PRINT_SIGNED(pack_current_deci_amps);
  PRINT_SIGNED(lowest_pack_current_deci_amps);
  PRINT_SIGNED(average_pack_current_deci_amps);
  PRINT_SIGNED(highest_pack_current_deci_amps);
  PRINT_SIGNED(lowest_pack_power_kw);
  PRINT_SIGNED(average_pack_power_kw);
  PRINT_SIGNED(highest_pack_power_kw);
  PRINT_UNSIGNED(pack_state_of_charge);
  PRINT_SIGNED(highest_temperature_c);
  PRINT_SIGNED(average_temperature_c);
  PRINT_SIGNED(lowest_temperature_c);
  PRINT_UNSIGNED(fault_flags);
#undef PRINT_UNSIGNED
#undef PRINT_SIGNED
  printf("}\n");
  fflush(stdout);
}

/**
 * Feed bounded stdin frame records into the HAL fake; retain real driver state across
 * input times.
 *
 * Inputs: stdin lines provide uint32 milliseconds, hexadecimal ID, 0/1 metadata and
 * canonical-length hex payload or -. Returns 1 for initialization/stream failure, 2 for
 * malformed records or zero at EOF. It resets once, feeds one frame per input line and
 * retains production telemetry/statistics between lines; output is one JSON snapshot per
 * poll. Unlike dashboard T parsing, this adapter does not enforce monotonic timestamps.
 */
static int replay_stdin(void) {
  static const uint8_t lengths[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
  char line[256];
  reset_state();
  if (!app_can_init()) {
    return 1;
  }
  while (fgets(line, sizeof(line), stdin) != NULL) {
    unsigned long long tick, identifier;
    unsigned extended, remote, fd;
    char hex[129], trailing;
    fake_frame_t frame = {0};
    if (sscanf(line,
               "%llu %llx %u %u %u %128s %c",
               &tick,
               &identifier,
               &extended,
               &remote,
               &fd,
               hex,
               &trailing) != 6 ||
        tick > UINT32_MAX || identifier > 0x1FFFFFFFU || extended > 1U || remote > 1U || fd > 1U) {
      fprintf(stderr, "Invalid replay record\n");
      return 2;
    }
    size_t bytes = strcmp(hex, "-") == 0 ? 0U : strlen(hex) / 2U;
    if (strcmp(hex, "-") != 0 && strlen(hex) % 2U != 0U) {
      return 2;
    }
    unsigned dlc = 0U;
    while (dlc < 16U && lengths[dlc] != bytes) {
      dlc++;
    }
    if (dlc == 16U) {
      fprintf(stderr, "Payload length has no CAN DLC\n");
      return 2;
    }
    for (size_t index = 0; index < bytes; index++) {
      unsigned value;
      if (!isxdigit((unsigned char)hex[index * 2U]) ||
          !isxdigit((unsigned char)hex[index * 2U + 1U]) ||
          sscanf(&hex[index * 2U], "%2x", &value) != 1) {
        return 2;
      }
      frame.payload[index] = (uint8_t)value;
    }
    frame.header.Identifier = (uint32_t)identifier;
    frame.header.IdType = extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    frame.header.RxFrameType = remote ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
    frame.header.FDFormat = fd ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    frame.header.DataLength = dlc;
    fake_tick = (uint32_t)tick;
    fake_rx_read = fake_rx_write = 0U;
    enqueue(frame);
    app_can_dashboard_data_t snapshot = {0};
    print_dashboard_json(app_can_poll_dashboard_data(&snapshot));
  }
  return ferror(stdin) ? 1 : 0;
}

/**
 * Run independent groups, a payload-overflow check, or the exclusively virtual stdin
 * replay adapter.
 *
 * Inputs: optional --replay or --overflow; otherwise executes the named test array.
 * Returns zero only when its chosen checks pass, one on failed groups and the replay
 * adapter's status for replay. CHECK counts assertions and returns false from a group at
 * first failure; main still runs remaining groups and reports totals.
 */
int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--replay") == 0) {
    return replay_stdin();
  }
  if (argc == 2 && strcmp(argv[1], "--overflow") == 0) {
    bool passed = test_oversized_hal_payload();
    printf("payload bounds: %s (%u assertions)\n", passed ? "PASS" : "FAIL", assertions);
    return passed ? 0 : 1;
  }
  const struct {
    const char *name;
    bool (*run)(void);
  } tests[] = {
      {"initialization and recovery", test_initialization_and_recovery},
      {"null guards and configuration", test_null_guards_and_configuration},
      {"all ten PID conversions", test_all_pid_conversions},
      {"signal freshness boundaries", test_signal_freshness_boundaries},
      {"per-PID independent validity and age", test_each_pid_has_independent_validity_and_age},
      {"invalid replies do not refresh signal age", test_invalid_replies_do_not_refresh_signal_age},
      {"exhaustive scalar domains", test_exhaustive_scalar_domains},
      {"exact single-frame lengths", test_exact_single_frame_lengths},
      {"frame metadata rejection", test_frame_metadata_rejection},
      {"DLC bounds and unknown messages", test_dlc_bounds_and_unknown_messages},
      {"power rounding and voltage fallback", test_power_rounding_and_voltage_fallback},
      {"current and power statistics", test_current_and_power_statistics},
      {"request cycle and payload", test_request_cycle_and_payload},
      {"timeout boundary", test_timeout_boundary},
      {"tick rollover", test_tick_rollover},
      {"TX backpressure and recovery", test_tx_backpressure_and_recovery},
      {"RX failure, FIFO burst and recovery", test_rx_failure_fifo_burst_and_recovery},
      {"unsolicited and malformed pending responses",
       test_unsolicited_and_malformed_pending_responses},
  };
  unsigned failures = 0U;
  for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    bool passed = tests[i].run();
    printf("%s: %s\n", tests[i].name, passed ? "PASS" : "FAIL");
    if (!passed) {
      failures++;
    }
  }
  printf("%zu groups, %u assertions, %u failures\n",
         sizeof(tests) / sizeof(tests[0]),
         assertions,
         failures);
  return failures == 0U ? 0 : 1;
}
