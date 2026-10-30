/* Purpose: Exercise the production eMMC logger against RAM storage and capture its UART wire
 * format. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../Src/app_emmc_log.c"

UART_HandleTypeDef hcom_uart[1];
static uint8_t storage[8][512];
static uint8_t wire[16384];
static size_t wire_count;
static uint32_t card_blocks = 4104U;
static bool fail_header_write;
static bool fail_data_write;
static bool fail_read;
static bool fail_uart;

/**
 * Report a successful fake initialization; no physical device is accessed.
 *
 * Input: instance must be zero or assertion aborts. Returns BSP_ERROR_NONE; storage erase
 * is done by main rather than this stub, allowing explicit test setup.
 */
int32_t BSP_MMC_Init(uint32_t instance) {
  assert(instance == 0U);
  return BSP_ERROR_NONE;
}

/**
 * Return the configured synthetic card capacity to bound production append operations.
 *
 * Inputs: instance zero and writable info. Returns BSP_ERROR_NONE after publishing
 * card_blocks; instance mismatch aborts. The selected capacity constrains the production
 * logger's reserved region.
 */
int32_t BSP_MMC_GetCardInfo(uint32_t instance, BSP_MMC_CardInfo *info) {
  assert(instance == 0U);
  info->LogBlockNbr = card_blocks;
  return BSP_ERROR_NONE;
}

/**
 * Copy requested fake blocks into the BSP caller, or return the injected read failure.
 *
 * Inputs: instance zero, writable out and a block/count inside the test card region.
 * Invalid harness ranges assert; injected fail_read returns -1 without copying. Success
 * copies count*512 bytes and returns BSP_ERROR_NONE.
 */
int32_t BSP_MMC_ReadBlocks(uint32_t instance, uint32_t *out, uint32_t block, uint32_t count) {
  assert(instance == 0U);
  assert(block >= 4096U && block + count <= card_blocks);
  if (fail_read) {
    return -1;
  }
  memcpy(out, storage[block - 4096U], count * 512U);
  return BSP_ERROR_NONE;
}

/**
 * Persist requested fake blocks, retaining prior bytes when a targeted write failure
 * occurs.
 *
 * Inputs: instance zero, readable in and an asserted valid region. Returns -1 before
 * copying when header/data-specific failure is enabled, preserving old bytes. Success
 * copies blocks and returns BSP_ERROR_NONE, letting retries be checked against persisted
 * contents.
 */
int32_t BSP_MMC_WriteBlocks(uint32_t instance, uint32_t *in, uint32_t block, uint32_t count) {
  assert(instance == 0U);
  assert(block >= 4096U && block + count <= card_blocks);
  if ((block == 4096U && fail_header_write) || (block != 4096U && fail_data_write)) {
    return -1;
  }
  memcpy(storage[block - 4096U], in, count * 512U);
  return BSP_ERROR_NONE;
}

/**
 * Append UART bytes to a bounded capture buffer, or return the injected transmit error.
 *
 * Inputs: bytes/count are binary export content; timeout must be HAL_MAX_DELAY, uart is
 * ignored. Returns HAL_ERROR for injected failure; otherwise bounds are asserted, bytes
 * are appended to wire and wire_count advances. It never writes a physical UART.
 */
HAL_StatusTypeDef
HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *bytes, uint16_t count, uint32_t timeout) {
  (void)uart;
  assert(timeout == HAL_MAX_DELAY);
  if (fail_uart) {
    return HAL_ERROR;
  }
  assert(wire_count + count <= sizeof(wire));
  memcpy(wire + wire_count, bytes, count);
  wire_count += count;
  return HAL_OK;
}

/**
 * Append a sample with recognizable signed and unsigned values for C-to-Python checks.
 *
 * Input: timestamp is milliseconds and also distinguishes the pack-voltage sample. Returns
 * nothing; constructs signed/unsigned boundary-rich telemetry and asserts production
 * append succeeds. Values are chosen for cross-language record decoding rather than
 * hardware simulation.
 */
static void append_sample(uint32_t timestamp) {
  app_can_dashboard_data_t data = {0};
  data.pack_voltage_deci_volts = (uint16_t)(4000U + timestamp);
  data.pack_summed_voltage_deci_volts = 3999U;
  data.lowest_cell_voltage_100uv = 30000U;
  data.average_cell_voltage_100uv = 35000U;
  data.highest_cell_voltage_100uv = 40000U;
  data.pack_current_deci_amps = -1234;
  data.lowest_pack_current_deci_amps = INT16_MIN;
  data.average_pack_current_deci_amps = -12;
  data.highest_pack_current_deci_amps = INT16_MAX;
  data.lowest_pack_power_kw = -99;
  data.average_pack_power_kw = -1;
  data.highest_pack_power_kw = 101;
  data.pack_state_of_charge = 87U;
  data.highest_temperature_c = 100;
  data.average_temperature_c = -10;
  data.lowest_temperature_c = INT8_MIN;
  data.fault_flags = 0xA5U;
  assert(app_emmc_log_append(timestamp, &data));
}

/**
 * Assert persisted header counters match a mutex-protected logger status snapshot.
 *
 * Inputs: none. Returns nothing or aborts; obtains the production mutex-protected status
 * and compares it to bytes persisted in block 4096. Also checks the logger's retained last
 * timestamp, exposing stale/failed header retries.
 */
static void check_header(void) {
  app_emmc_log_status_t status = {0};
  app_emmc_log_header_t header = {0};
  app_emmc_log_get_status(&status);
  memcpy(&header, storage[0], sizeof(header));
  assert(header.record_count == status.record_count);
  assert(header.written_blocks == status.written_blocks);
  assert(header.last_timestamp_ms == app_emmc_log_last_timestamp_ms);
}

/**
 * Check that UI storage usage does not add a retained tail to an already counted block.
 *
 * Input: expected is the unallocated staged byte count. Returns nothing or aborts;
 * compares a production status snapshot without altering storage. Already-counted
 * retained-tail bytes must not inflate usage.
 */
static void check_staged_bytes(uint32_t expected) {
  app_emmc_log_status_t status = {0};
  app_emmc_log_get_status(&status);
  assert(status.staged_bytes == expected);
}

/**
 * Run a requested deterministic logger regression and emit only its binary export to
 * stdout.
 *
 * Input: one scenario name:
 * partial/corrupt/csv/slots/automatic/reset/status/capacity/errors/empty. Returns zero
 * only if all asserted invariants hold. It resets fake blocks, initializes production
 * logger, verifies CRC32, exercises selected append/flush/reset/error paths, then exports
 * captured binary bytes to stdout (CSV mode calls production CSV instead). No failure is
 * suppressed; assertions abort before emitting a successful result.
 */
int main(int argc, char **argv) {
  assert(argc == 2);
  memset(storage, 0xFF, sizeof(storage));
  if (strcmp(argv[1], "capacity") == 0) {
    card_blocks = 4098U;
  }
  assert(app_emmc_log_init());
  assert(app_emmc_log_crc32_compute((const uint8_t *)"123456789", 9U) == 0xCBF43926U);

  if (strcmp(argv[1], "partial") == 0 || strcmp(argv[1], "corrupt") == 0 ||
      strcmp(argv[1], "csv") == 0) {
    append_sample(11U);
    assert(app_emmc_log_flush());
    append_sample(22U);
    assert(app_emmc_log_flush());
    if (strcmp(argv[1], "corrupt") == 0) {
      storage[1][0] ^= 1U;
    }
  } else if (strcmp(argv[1], "slots") == 0) {
    for (uint32_t index = 1U; index <= 11U; index++) {
      append_sample(index);
      assert(app_emmc_log_flush());
    }
    assert(app_emmc_log_written_blocks == 1U);
    assert(app_emmc_log_dump_binary());
    wire_count = 0U;
    append_sample(12U);
    assert(app_emmc_log_flush());
    assert(app_emmc_log_written_blocks == 1U);
    assert(app_emmc_log_block_fill == 0U);
    append_sample(13U);
    assert(app_emmc_log_flush());
    assert(app_emmc_log_written_blocks == 2U);
    assert(app_emmc_log_flush());
    check_header();
  } else if (strcmp(argv[1], "automatic") == 0) {
    for (uint32_t index = 1U; index <= 13U; index++) {
      append_sample(index);
    }
    assert(app_emmc_log_flush());
    assert(app_emmc_log_written_blocks == 2U);
    check_header();
  } else if (strcmp(argv[1], "reset") == 0) {
    append_sample(11U);
    assert(app_emmc_log_flush());
    assert(app_emmc_log_reset_session());
    assert(app_emmc_log_session_id == 2U);
    assert(app_emmc_log_written_blocks == 0U);
    assert(app_emmc_log_record_count == 0U);
    append_sample(99U);
  } else if (strcmp(argv[1], "status") == 0) {
    append_sample(1U);
    check_staged_bytes(40U);
    assert(app_emmc_log_flush());
    check_staged_bytes(0U);
    append_sample(2U);
    check_staged_bytes(0U);
    assert(app_emmc_log_flush());
    check_staged_bytes(0U);
    for (uint32_t index = 3U; index <= 12U; index++) {
      append_sample(index);
    }
    assert(app_emmc_log_flush());
    check_staged_bytes(0U);
    append_sample(13U);
    check_staged_bytes(40U);
  } else if (strcmp(argv[1], "capacity") == 0) {
    for (uint32_t index = 1U; index <= 12U; index++) {
      append_sample(index);
      assert(app_emmc_log_flush());
    }
    app_can_dashboard_data_t data = {0};
    assert(!app_emmc_log_append(13U, &data));
    assert(app_emmc_log_record_count == 12U);
    assert(app_emmc_log_written_blocks == 1U);
    assert(app_emmc_log_overflow);
  } else if (strcmp(argv[1], "errors") == 0) {
    append_sample(1U);
    fail_data_write = true;
    assert(!app_emmc_log_flush());
    assert(app_emmc_log_written_blocks == 0U);
    fail_data_write = false;
    fail_header_write = true;
    assert(!app_emmc_log_flush());
    assert(app_emmc_log_written_blocks == 1U);
    fail_header_write = false;
    assert(app_emmc_log_flush());
    check_header();
    for (uint32_t index = 2U; index <= 12U; index++) {
      append_sample(index);
    }
    fail_header_write = true;
    assert(!app_emmc_log_flush());
    assert(app_emmc_log_block_fill == 0U);
    fail_header_write = false;
    assert(app_emmc_log_flush());
    check_header();
    fail_read = true;
    assert(!app_emmc_log_dump_binary());
    fail_read = false;
    wire_count = 0U;
    fail_uart = true;
    assert(!app_emmc_log_dump_binary());
    fail_uart = false;
  } else {
    assert(strcmp(argv[1], "empty") == 0);
    assert(app_emmc_log_flush());
    assert(app_emmc_log_flush());
  }

  if (strcmp(argv[1], "csv") == 0) {
    app_emmc_log_dump_csv();
    return 0;
  }
  assert(app_emmc_log_dump_binary());
  assert(fwrite(wire, 1U, wire_count, stdout) == wire_count);
  return 0;
}

/** Host fake card writes complete synchronously. */
int32_t BSP_MMC_GetCardState(uint32_t instance) {
  (void)instance;
  return MMC_TRANSFER_OK;
}
