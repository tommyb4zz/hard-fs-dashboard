/**
 * File purpose: Store millisecond-timestamped native records with CRCs in a reserved
 * block-addressed eMMC region. A FreeRTOS mutex protects staging, partial-tail rewrites, session
 * counters and synchronous CSV/binary export. Explicit little-endian wire serialization
 * decouples export from native struct padding. Initialization starts a new session
 * rather than recovering prior data; append success can mean RAM-only staging.
 */

#include "app_emmc_log.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "stm32h745i_discovery.h"
#include "stm32h745i_discovery_mmc.h"

/* BSP logical addressing is in 512-byte blocks. The reservation starts at block 4096
 * (2 MiB) and requests 262144 blocks (128 MiB), including its one metadata header block.
 * Initialization limits this reservation to reported card capacity; no filesystem
 * reservation is implemented here. */
#define APP_EMMC_LOG_INSTANCE 0U
#define APP_EMMC_LOG_BLOCK_SIZE 512U
#define APP_EMMC_LOG_START_BLOCK 4096U
#define APP_EMMC_LOG_RESERVED_BLOCKS 262144U
#define APP_EMMC_LOG_HEADER_MAGIC 0x48464C47UL
#define APP_EMMC_LOG_HEADER_VERSION 1U
#define APP_EMMC_LOG_FLAG_ACTIVE 0x00000001UL
#define APP_EMMC_LOG_FLAG_OVERFLOW 0x00000002UL
#define APP_EMMC_LOG_DUMP_HEADER_SIZE 24U
#define APP_EMMC_LOG_DUMP_RECORD_SIZE 42U
#define APP_EMMC_LOG_DUMP_FOOTER_SIZE 16U

static const uint8_t app_emmc_log_dump_header_magic[4] = {'H', 'F', 'D', 'B'};
static const uint8_t app_emmc_log_dump_footer_magic[4] = {'H', 'F', 'D', 'E'};
static const uint8_t app_emmc_log_dump_record_crc_valid_flag = 0x01U;

/* Native storage layout includes ABI padding before crc32; it differs from 42-byte export. */
typedef struct {
  uint32_t timestamp_ms;
  app_can_dashboard_data_t dashboard_data;
  uint32_t crc32;
} app_emmc_log_record_t;

/* Exactly one 512-byte session header: ten uint32_t fields and unused reserved bytes.
 * Header has magic/version but no integrity CRC or power-loss journal in this format. */
typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t session_id;
  uint32_t record_size;
  uint32_t start_block;
  uint32_t reserved_blocks;
  uint32_t written_blocks;
  uint32_t record_count;
  uint32_t flags;
  uint32_t last_timestamp_ms;
  uint8_t reserved[APP_EMMC_LOG_BLOCK_SIZE - (10U * sizeof(uint32_t))];
} app_emmc_log_header_t;

/* Word alignment satisfies BSP block pointers while bytes support staging/serialization. */
typedef union {
  uint32_t words[APP_EMMC_LOG_BLOCK_SIZE / sizeof(uint32_t)];
  uint8_t bytes[APP_EMMC_LOG_BLOCK_SIZE];
  app_emmc_log_header_t header;
} app_emmc_log_block_t;

/* Module state is mutex-owned. block_fill retains partial-tail bytes after flush;
 * tail_block_written selects rewrite of the allocated tail rather than allocating again.
 * block_dirty and header_dirty allow independent data/header retry after write failure. */
static SemaphoreHandle_t app_emmc_log_mutex = NULL;
static bool app_emmc_log_ready = false;
static uint32_t app_emmc_log_session_id = 0U;
static uint32_t app_emmc_log_written_blocks = 0U;
static uint32_t app_emmc_log_record_count = 0U;
static uint32_t app_emmc_log_last_timestamp_ms = 0U;
static uint32_t app_emmc_log_reserved_blocks = APP_EMMC_LOG_RESERVED_BLOCKS;
static uint32_t app_emmc_log_block_fill = 0U;
static uint32_t app_emmc_log_record_size = sizeof(app_emmc_log_record_t);
static bool app_emmc_log_overflow = false;
static bool app_emmc_log_tail_block_written = false;
static bool app_emmc_log_block_dirty = false;
static bool app_emmc_log_header_dirty = false;
static app_emmc_log_block_t app_emmc_log_block_buffer = {0};

/**
 * Take the logger mutex with an indefinite FreeRTOS wait; return false if the mutex has
 * not been created or the take fails.
 *
 * There are no arguments; successful return means the caller owns the single module mutex.
 * portMAX_DELAY can block the calling task without a finite timeout. Helpers ending
 * _locked rely on this ownership rather than taking the mutex again.
 */
static bool app_emmc_log_lock(void) {
  if (app_emmc_log_mutex == NULL) {
    return false;
  }

  return xSemaphoreTake(app_emmc_log_mutex, portMAX_DELAY) == pdTRUE;
}

/**
 * Release the logger mutex if it exists; ignore the FreeRTOS give result.
 *
 * This balances a successful lock or already-held helper chain. There is no return value
 * or owner validation in this wrapper; it does not flush records or alter logger
 * readiness.
 */
static void app_emmc_log_unlock(void) {
  if (app_emmc_log_mutex != NULL) {
    (void)xSemaphoreGive(app_emmc_log_mutex);
  }
}

/**
 * Update a caller-supplied, unfinalized reflected CRC-32 accumulator with size bytes using
 * polynomial 0xEDB88320; return the unchanged accumulator for null data.
 *
 * crc is the previous raw accumulator, size is bytes, and data must expose that many
 * bytes. XOR each byte into the low bits and perform eight reflected polynomial shifts. Do
 * not initialize or complement here; callers choose whether this is a whole-buffer or
 * concatenated-stream calculation.
 */
static uint32_t app_emmc_log_crc32_update(uint32_t crc, const uint8_t *data, uint32_t size) {
  uint32_t index = 0U;
  uint32_t bit = 0U;

  if (data == NULL) {
    return crc;
  }

  for (index = 0U; index < size; index++) {
    crc ^= (uint32_t)data[index];
    for (bit = 0U; bit < 8U; bit++) {
      if ((crc & 1U) != 0U) {
        crc = (crc >> 1U) ^ 0xEDB88320UL;
      } else {
        crc >>= 1U;
      }
    }
  }

  return crc;
}

/**
 * Return a CRC-32 computed from size bytes with all-one initialization and final XOR,
 * using the logger CRC update routine.
 *
 * Use seed 0xFFFFFFFF and complement the returned update accumulator to create the
 * finalized CRC. The underlying updater returns its seed for null data, so this wrapper
 * returns zero for null data rather than an error code.
 */
static uint32_t app_emmc_log_crc32_compute(const uint8_t *data, uint32_t size) {
  return app_emmc_log_crc32_update(0xFFFFFFFFUL, data, size) ^ 0xFFFFFFFFUL;
}

/**
 * Return the CRC-32 of the in-memory record bytes before its crc32 member, including any
 * structure padding; return zero for null record.
 *
 * The record is the native storage structure, not the 42-byte portable wire record.
 * offsetof excludes the stored checksum field; padding before that field participates in
 * the checksum. Callers zero-initialize records before populating fields; padding still
 * participates in the checksum.
 */
static uint32_t app_emmc_log_record_crc32(const app_emmc_log_record_t *record) {
  if (record == NULL) {
    return 0U;
  }

  return app_emmc_log_crc32_compute((const uint8_t *)record,
                                    (uint32_t)offsetof(app_emmc_log_record_t, crc32));
}

/**
 * Compare the stored record CRC with a recalculated CRC over the preceding bytes; return
 * false for a null record.
 *
 * record points to a copied native storage record. The result is an integrity comparison
 * only; it neither repairs corrupted content nor checks measurement ranges or
 * header/session validity.
 */
static bool app_emmc_log_record_crc_is_valid(const app_emmc_log_record_t *record) {
  if (record == NULL) {
    return false;
  }

  return record->crc32 == app_emmc_log_record_crc32(record);
}

/**
 * Write value into the caller-provided two-byte buffer in little-endian order; the buffer
 * must be valid.
 *
 * buffer must expose at least two writable bytes; shifts/masks explicitly encode low byte
 * first independent of host structure layout. No bounds/null check or state beyond those
 * two bytes is provided.
 */
static void app_emmc_log_write_le16(uint8_t *buffer, uint16_t value) {
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

/**
 * Write value into the caller-provided four-byte buffer in little-endian order; the buffer
 * must be valid.
 *
 * buffer must expose at least four writable bytes; each byte is extracted explicitly in
 * increasing significance. No allocation, bounds/null check or external I/O occurs.
 */
static void app_emmc_log_write_le32(uint8_t *buffer, uint32_t value) {
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
  buffer[2] = (uint8_t)((value >> 16U) & 0xFFU);
  buffer[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

/**
 * Transmit size bytes synchronously on COM1 with HAL_MAX_DELAY; return false for
 * null/empty data or a HAL UART failure, otherwise true.
 *
 * size is a uint16_t byte count and data must remain valid during the synchronous send.
 * Success means HAL_UART_Transmit returned HAL_OK, not that a receiver validated the
 * payload. The indefinite HAL timeout can hold an export and its logger mutex.
 */
static bool app_emmc_log_uart_write_bytes(const uint8_t *data, uint16_t size) {
  if ((data == NULL) || (size == 0U)) {
    return false;
  }

  return HAL_UART_Transmit(&hcom_uart[COM1], (uint8_t *)data, size, HAL_MAX_DELAY) == HAL_OK;
}

/**
 * Populate the supplied storage header from current session counters, configured region
 * and overflow state after clearing it; ignore a null header.
 *
 * header must be writable storage for the 512-byte header. Fill magic/version,
 * region/session identity, native record size, counts, active/overflow flags and latest
 * millisecond timestamp; unused bytes become zero. This builds metadata in RAM and adds no
 * header CRC or persistence itself.
 */
static void app_emmc_log_build_header(app_emmc_log_header_t *header) {
  if (header == NULL) {
    return;
  }

  memset(header, 0, sizeof(*header));
  header->magic = APP_EMMC_LOG_HEADER_MAGIC;
  header->version = APP_EMMC_LOG_HEADER_VERSION;
  header->session_id = app_emmc_log_session_id;
  header->record_size = app_emmc_log_record_size;
  header->start_block = APP_EMMC_LOG_START_BLOCK;
  header->reserved_blocks = app_emmc_log_reserved_blocks;
  header->written_blocks = app_emmc_log_written_blocks;
  header->record_count = app_emmc_log_record_count;
  header->flags = APP_EMMC_LOG_FLAG_ACTIVE;
  if (app_emmc_log_overflow) {
    header->flags |= APP_EMMC_LOG_FLAG_OVERFLOW;
  }
  header->last_timestamp_ms = app_emmc_log_last_timestamp_ms;
}

/**
 * Write the current session header to the reserved eMMC start block and return the BSP
 * write result as a boolean; the caller must hold the logger mutex.
 *
 * Write exactly one 512-byte block at absolute logical block 4096 using BSP instance zero.
 * Successful write clears header_dirty; failure leaves it available for retry. Header
 * counters describe current RAM state, including accepted staged records, and no power-
 * loss transaction is implemented.
 */
static bool app_emmc_log_write_header_locked(void) {
  app_emmc_log_block_t header_block = {0};

  memset(&header_block, 0xFF, sizeof(header_block));
  app_emmc_log_build_header(&header_block.header);

  if (BSP_MMC_WriteBlocks(
          APP_EMMC_LOG_INSTANCE, header_block.words, APP_EMMC_LOG_START_BLOCK, 1U) !=
      BSP_ERROR_NONE) {
    return false;
  }
  app_emmc_log_header_dirty = false;
  return true;
}

/**
 * While the caller holds the mutex, persist dirty staged records and the session header.
 * Keep a partial tail in RAM so later records rewrite its remaining slots, counting its
 * physical block once. Release a full tail; retry a failed header even with no dirty data.
 * Return false if unready, out of reserved blocks or a write fails.
 *
 * The 512-byte data block is padded with 0xFF beyond block_fill. A new tail is written at
 * start+1+written_blocks; an already-written partial tail reuses the prior address.
 * Increment physical block count only on its first write, retain partial contents for
 * later append, and release the buffer when another native record cannot fit. On data-
 * write failure leave dirty state for retry; on header failure the data may already be
 * persisted. Overflow marks metadata and attempts its header write before returning false.
 */
static bool app_emmc_log_flush_locked(void) {
  uint32_t target_block = 0U;
  app_emmc_log_block_t block = {0};

  if (!app_emmc_log_ready) {
    return false;
  }

  if (!app_emmc_log_block_dirty) {
    return !app_emmc_log_header_dirty || app_emmc_log_write_header_locked();
  }

  if (!app_emmc_log_tail_block_written &&
      ((app_emmc_log_written_blocks + 1U) >= app_emmc_log_reserved_blocks)) {
    app_emmc_log_overflow = true;
    app_emmc_log_header_dirty = true;
    (void)app_emmc_log_write_header_locked();
    return false;
  }

  memset(&block, 0xFF, sizeof(block));
  memcpy(block.bytes, app_emmc_log_block_buffer.bytes, app_emmc_log_block_fill);

  target_block = APP_EMMC_LOG_START_BLOCK + 1U + app_emmc_log_written_blocks;
  if (app_emmc_log_tail_block_written) {
    target_block--;
  }
  if (BSP_MMC_WriteBlocks(APP_EMMC_LOG_INSTANCE, block.words, target_block, 1U) != BSP_ERROR_NONE) {
    return false;
  }

  if (!app_emmc_log_tail_block_written) {
    app_emmc_log_written_blocks++;
  }
  app_emmc_log_tail_block_written = true;
  app_emmc_log_block_dirty = false;
  app_emmc_log_header_dirty = true;
  if ((app_emmc_log_block_fill + app_emmc_log_record_size) > APP_EMMC_LOG_BLOCK_SIZE) {
    app_emmc_log_block_fill = 0U;
    app_emmc_log_tail_block_written = false;
    memset(&app_emmc_log_block_buffer, 0, sizeof(app_emmc_log_block_buffer));
  }
  return app_emmc_log_write_header_locked();
}

/**
 * Replace the current session identifier and clear record/block counters, staged bytes and
 * overflow, then write the new eMMC header; return the write result. The caller must hold
 * the mutex.
 *
 * next_session_id is used exactly as supplied, including unsigned rollover. Discard staged
 * data and logical history by resetting counters and flags; existing data blocks are not
 * erased. Header write failure returns false after RAM state has already changed; callers
 * update readiness accordingly.
 */
static bool app_emmc_log_start_new_session_locked(uint32_t next_session_id) {
  app_emmc_log_session_id = next_session_id;
  app_emmc_log_written_blocks = 0U;
  app_emmc_log_record_count = 0U;
  app_emmc_log_last_timestamp_ms = 0U;
  app_emmc_log_block_fill = 0U;
  app_emmc_log_overflow = false;
  app_emmc_log_tail_block_written = false;
  app_emmc_log_block_dirty = false;
  app_emmc_log_header_dirty = true;
  memset(&app_emmc_log_block_buffer, 0, sizeof(app_emmc_log_block_buffer));
  return app_emmc_log_write_header_locked();
}

/**
 * Return how many in-memory records fit in one eMMC block, or zero when record size is
 * zero.
 *
 * Integer division truncates the unused padding at the end of each 512-byte block.
 * Consumers use this quotient to map logical record index to block and slot, avoiding
 * interpretation of padding as records. The size is sizeof the native record, not portable
 * export size.
 */
static uint32_t app_emmc_log_get_records_per_block(void) {
  if (app_emmc_log_record_size == 0U) {
    return 0U;
  }

  return APP_EMMC_LOG_BLOCK_SIZE / app_emmc_log_record_size;
}

/**
 * Serialize a record into the caller-provided dump buffer in little-endian field order,
 * including stored-CRC validity, stored CRC and a new dump CRC; return bytes written, or
 * zero for null input/output.
 *
 * buffer must have 42 writable bytes; encode timestamp, scaled dashboard fields, integrity
 * flag, stored native CRC and finalized portable CRC. Signed 16-bit values preserve their
 * bit pattern through uint16_t conversion and signed temperatures occupy one byte. The
 * portable layout contains no native padding. stored_crc_valid is reported as supplied and
 * does not prevent serialization of corrupted storage content.
 */
static uint32_t app_emmc_log_pack_dump_record(uint8_t *buffer,
                                              const app_emmc_log_record_t *record,
                                              bool stored_crc_valid) {
  uint32_t offset = 0U;
  uint32_t crc32 = 0U;

  if ((buffer == NULL) || (record == NULL)) {
    return 0U;
  }

  app_emmc_log_write_le32(&buffer[offset], record->timestamp_ms);
  offset += 4U;

  app_emmc_log_write_le16(&buffer[offset], record->dashboard_data.pack_voltage_deci_volts);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], record->dashboard_data.pack_summed_voltage_deci_volts);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], record->dashboard_data.lowest_cell_voltage_100uv);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], record->dashboard_data.average_cell_voltage_100uv);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], record->dashboard_data.highest_cell_voltage_100uv);
  offset += 2U;

  app_emmc_log_write_le16(&buffer[offset], (uint16_t)record->dashboard_data.pack_current_deci_amps);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset],
                          (uint16_t)record->dashboard_data.lowest_pack_current_deci_amps);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset],
                          (uint16_t)record->dashboard_data.average_pack_current_deci_amps);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset],
                          (uint16_t)record->dashboard_data.highest_pack_current_deci_amps);
  offset += 2U;

  app_emmc_log_write_le16(&buffer[offset], (uint16_t)record->dashboard_data.lowest_pack_power_kw);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], (uint16_t)record->dashboard_data.average_pack_power_kw);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], (uint16_t)record->dashboard_data.highest_pack_power_kw);
  offset += 2U;

  buffer[offset++] = record->dashboard_data.pack_state_of_charge;
  buffer[offset++] = (uint8_t)record->dashboard_data.highest_temperature_c;
  buffer[offset++] = (uint8_t)record->dashboard_data.average_temperature_c;
  buffer[offset++] = (uint8_t)record->dashboard_data.lowest_temperature_c;
  buffer[offset++] = record->dashboard_data.fault_flags;
  buffer[offset++] = stored_crc_valid ? app_emmc_log_dump_record_crc_valid_flag : 0U;

  app_emmc_log_write_le32(&buffer[offset], record->crc32);
  offset += 4U;

  crc32 = app_emmc_log_crc32_compute(buffer, offset);
  app_emmc_log_write_le32(&buffer[offset], crc32);
  offset += 4U;

  return offset;
}

/**
 * Create/take the logger mutex, initialize the eMMC BSP and size the reserved region to
 * the card. Read the prior header only to choose the next session ID, then start a new
 * session; return readiness and release the mutex on all locked paths.
 *
 * No prior-session records are resumed. Cap the requested 262144-block region against
 * reported LogBlockNbr after requiring space beyond start+one header; recognize only
 * magic/version in a readable old header for next session ID. Allocation, lock, BSP
 * init/card-info/capacity or new-header failure produces false. Successful init sets
 * readiness and a new empty session; it does not prove reserved-region ownership or
 * physical card identity.
 */
bool app_emmc_log_init(void) {
  BSP_MMC_CardInfo card_info = {0};
  app_emmc_log_block_t header_block = {0};
  uint32_t next_session_id = 1U;

  if (app_emmc_log_mutex == NULL) {
    app_emmc_log_mutex = xSemaphoreCreateMutex();
    if (app_emmc_log_mutex == NULL) {
      return false;
    }
  }

  if (!app_emmc_log_lock()) {
    return false;
  }

  if (BSP_MMC_Init(APP_EMMC_LOG_INSTANCE) != BSP_ERROR_NONE) {
    app_emmc_log_ready = false;
    app_emmc_log_unlock();
    return false;
  }

  if (BSP_MMC_GetCardInfo(APP_EMMC_LOG_INSTANCE, &card_info) != BSP_ERROR_NONE) {
    app_emmc_log_ready = false;
    app_emmc_log_unlock();
    return false;
  }

  if (card_info.LogBlockNbr <= (APP_EMMC_LOG_START_BLOCK + 1U)) {
    app_emmc_log_ready = false;
    app_emmc_log_unlock();
    return false;
  }

  app_emmc_log_reserved_blocks = APP_EMMC_LOG_RESERVED_BLOCKS;
  if (APP_EMMC_LOG_START_BLOCK + app_emmc_log_reserved_blocks > card_info.LogBlockNbr) {
    app_emmc_log_reserved_blocks = card_info.LogBlockNbr - APP_EMMC_LOG_START_BLOCK;
  }

  if (BSP_MMC_ReadBlocks(APP_EMMC_LOG_INSTANCE, header_block.words, APP_EMMC_LOG_START_BLOCK, 1U) ==
      BSP_ERROR_NONE) {
    if ((header_block.header.magic == APP_EMMC_LOG_HEADER_MAGIC) &&
        (header_block.header.version == APP_EMMC_LOG_HEADER_VERSION)) {
      next_session_id = header_block.header.session_id + 1U;
    }
  }

  app_emmc_log_ready = app_emmc_log_start_new_session_locked(next_session_id);
  app_emmc_log_unlock();
  return app_emmc_log_ready;
}

/**
 * Stage a timestamped copy of data with its storage CRC under the mutex, flushing a block
 * when necessary and advancing session counters; return false for invalid/unready input or
 * lock/flush failure. Success can leave the record staged in RAM.
 *
 * timestamp_ms is copied as milliseconds without monotonicity checks and data is copied by
 * value. A zero-initialized native record gets a CRC before staging; acceptance increments
 * logical count immediately. If a later automatic flush fails, false can follow
 * staging/count mutation. A normal true result can mean RAM-only staging; durability
 * requires flush. Null data, oversized record, unready logger, locking, reserved-space or
 * flushing errors return false.
 */
bool app_emmc_log_append(uint32_t timestamp_ms, const app_can_dashboard_data_t *data) {
  app_emmc_log_record_t record = {0};

  if ((data == NULL) || !app_emmc_log_ready) {
    return false;
  }

  if (app_emmc_log_record_size > APP_EMMC_LOG_BLOCK_SIZE) {
    return false;
  }

  if (!app_emmc_log_lock()) {
    return false;
  }

  if ((app_emmc_log_block_fill + app_emmc_log_record_size) > APP_EMMC_LOG_BLOCK_SIZE) {
    if (!app_emmc_log_flush_locked()) {
      app_emmc_log_unlock();
      return false;
    }
  }

  if (!app_emmc_log_tail_block_written && (app_emmc_log_block_fill == 0U) &&
      ((app_emmc_log_written_blocks + 1U) >= app_emmc_log_reserved_blocks)) {
    app_emmc_log_overflow = true;
    app_emmc_log_header_dirty = true;
    (void)app_emmc_log_write_header_locked();
    app_emmc_log_unlock();
    return false;
  }

  record.timestamp_ms = timestamp_ms;
  record.dashboard_data = *data;
  record.crc32 = app_emmc_log_record_crc32(&record);

  memcpy(&app_emmc_log_block_buffer.bytes[app_emmc_log_block_fill], &record, sizeof(record));
  app_emmc_log_block_fill += app_emmc_log_record_size;
  app_emmc_log_record_count++;
  app_emmc_log_last_timestamp_ms = timestamp_ms;
  app_emmc_log_block_dirty = true;
  app_emmc_log_header_dirty = true;

  if (app_emmc_log_block_fill == APP_EMMC_LOG_BLOCK_SIZE) {
    if (!app_emmc_log_flush_locked()) {
      app_emmc_log_unlock();
      return false;
    }
  }

  app_emmc_log_unlock();
  return true;
}

/**
 * Take the mutex, flush staged eMMC records and release the mutex; return false if locking
 * or flushing fails, otherwise true.
 *
 * This public wrapper does not require caller mutex ownership. The locked helper can
 * rewrite a partial data tail or only retry dirty metadata; no staged data and clean
 * header succeed when ready. The returned boolean does not report how many records/bytes
 * were persisted.
 */
bool app_emmc_log_flush(void) {
  bool result = false;

  if (!app_emmc_log_lock()) {
    return false;
  }

  result = app_emmc_log_flush_locked();
  app_emmc_log_unlock();
  return result;
}

/**
 * Under the mutex, increment the session ID, discard staged records and reset counters by
 * writing a new header; update readiness and return the header write result.
 *
 * No arguments; use unsigned session_id+1 and replace logical history with a new empty
 * session. The reset does not erase all reserved blocks or preserve staged records. Lock
 * failure returns false without reset, while header failure has already cleared session
 * state and marks readiness false.
 */
bool app_emmc_log_reset_session(void) {
  bool result = false;
  uint32_t next_session_id = 1U;

  if (!app_emmc_log_lock()) {
    return false;
  }

  next_session_id = app_emmc_log_session_id + 1U;
  result = app_emmc_log_start_new_session_locked(next_session_id);
  app_emmc_log_ready = result;
  app_emmc_log_unlock();
  return result;
}

/**
 * Clear and populate the caller status structure with a mutex-protected logger snapshot;
 * ignore null status and leave zeros if locking fails.
 *
 * status is caller writable storage. Clear it first so a missing/failed mutex leaves an
 * all-zero snapshot; on success copy readiness/overflow/session/count/block fields
 * atomically under the mutex. staged_bytes counts only an unallocated tail: a retained
 * already-written tail contributes zero even after another append, avoiding double-counted
 * allocated storage.
 */
void app_emmc_log_get_status(app_emmc_log_status_t *status) {
  if (status == NULL) {
    return;
  }

  memset(status, 0, sizeof(*status));
  if (!app_emmc_log_lock()) {
    return;
  }

  status->ready = app_emmc_log_ready;
  status->overflow = app_emmc_log_overflow;
  status->session_id = app_emmc_log_session_id;
  status->record_count = app_emmc_log_record_count;
  status->written_blocks = app_emmc_log_written_blocks;
  status->reserved_blocks = app_emmc_log_reserved_blocks;
  /* A retained tail is already included in written_blocks, even after another append. */
  status->staged_bytes = app_emmc_log_tail_block_written ? 0U : app_emmc_log_block_fill;
  app_emmc_log_unlock();
}

/**
 * Hold the logger mutex, flush staged records, read stored blocks and print each sample
 * with its stored-CRC validity. Return without output when setup fails and stop rows at a
 * block read failure.
 *
 * Logical records are mapped using records-per-block and cached one block at a time.
 * Validate native CRC and print the boolean even for invalid records; integer fields keep
 * storage units. Read failure yields a truncated CSV with no boolean return. The mutex
 * remains held through flushing, reads and printf, preventing concurrent append/status
 * operations.
 */
void app_emmc_log_dump_csv(void) {
  app_emmc_log_block_t block = {0};
  app_emmc_log_record_t record = {0};
  bool record_crc_valid = false;
  uint32_t records_per_block = 0U;
  uint32_t record_index = 0U;
  uint32_t cached_block_index = UINT32_MAX;
  uint32_t block_index = 0U;
  uint32_t offset_in_block = 0U;

  if (!app_emmc_log_lock()) {
    return;
  }

  if (!app_emmc_log_ready) {
    app_emmc_log_unlock();
    return;
  }

  if (!app_emmc_log_flush_locked()) {
    app_emmc_log_unlock();
    return;
  }

  records_per_block = app_emmc_log_get_records_per_block();
  if (records_per_block == 0U) {
    app_emmc_log_unlock();
    return;
  }

  printf("timestamp_ms,pack_voltage_deci_v,pack_summed_voltage_deci_v,"
         "lowest_cell_voltage_100uv,average_cell_voltage_100uv,highest_cell_voltage_100uv,"
         "pack_current_deci_a,lowest_pack_current_deci_a,average_pack_current_deci_a,"
         "highest_pack_current_deci_a,lowest_pack_power_kw,average_pack_power_kw,"
         "highest_pack_power_kw,pack_soc,highest_temp_c,average_temp_c,lowest_temp_c,"
         "fault_flags,record_crc_valid\r\n");

  for (record_index = 0U; record_index < app_emmc_log_record_count; record_index++) {
    block_index = record_index / records_per_block;
    offset_in_block = (record_index % records_per_block) * app_emmc_log_record_size;

    if (block_index != cached_block_index) {
      memset(&block, 0, sizeof(block));
      if (BSP_MMC_ReadBlocks(APP_EMMC_LOG_INSTANCE,
                             block.words,
                             APP_EMMC_LOG_START_BLOCK + 1U + block_index,
                             1U) != BSP_ERROR_NONE) {
        break;
      }
      cached_block_index = block_index;
    }

    memcpy(&record, &block.bytes[offset_in_block], sizeof(record));
    record_crc_valid = app_emmc_log_record_crc_is_valid(&record);
    printf("%lu,%u,%u,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%u,%u\r\n",
           (unsigned long)record.timestamp_ms,
           (unsigned int)record.dashboard_data.pack_voltage_deci_volts,
           (unsigned int)record.dashboard_data.pack_summed_voltage_deci_volts,
           (unsigned int)record.dashboard_data.lowest_cell_voltage_100uv,
           (unsigned int)record.dashboard_data.average_cell_voltage_100uv,
           (unsigned int)record.dashboard_data.highest_cell_voltage_100uv,
           (int)record.dashboard_data.pack_current_deci_amps,
           (int)record.dashboard_data.lowest_pack_current_deci_amps,
           (int)record.dashboard_data.average_pack_current_deci_amps,
           (int)record.dashboard_data.highest_pack_current_deci_amps,
           (int)record.dashboard_data.lowest_pack_power_kw,
           (int)record.dashboard_data.average_pack_power_kw,
           (int)record.dashboard_data.highest_pack_power_kw,
           (unsigned int)record.dashboard_data.pack_state_of_charge,
           (int)record.dashboard_data.highest_temperature_c,
           (int)record.dashboard_data.average_temperature_c,
           (int)record.dashboard_data.lowest_temperature_c,
           (unsigned int)record.dashboard_data.fault_flags,
           record_crc_valid ? 1U : 0U);
  }

  app_emmc_log_unlock();
}

/**
 * Hold the logger mutex and flush staged data, then transmit the HFDB header, packed
 * records and HFDE footer on COM1 with header/record/stream/footer CRCs; return false on
 * lock, storage, packing or UART failure, otherwise true.
 *
 * Emit a 24-byte HFDB header, record_count portable 42-byte rows and a 16-byte HFDE
 * footer, all little-endian. Header CRC covers its first 20 bytes; row CRC covers first
 * 38; stream CRC covers complete rows using one raw accumulator then final XOR; footer CRC
 * covers first 12. Stored native-CRC validity is a row flag and does not abort export. Any
 * error may leave a partial stream without footer, so the receiver must validate count and
 * all checksums. The mutex stays held through synchronous UART output.
 */
bool app_emmc_log_dump_binary(void) {
  app_emmc_log_block_t block = {0};
  app_emmc_log_record_t record = {0};
  uint8_t header[APP_EMMC_LOG_DUMP_HEADER_SIZE] = {0};
  uint8_t footer[APP_EMMC_LOG_DUMP_FOOTER_SIZE] = {0};
  uint8_t packed_record[APP_EMMC_LOG_DUMP_RECORD_SIZE] = {0};
  uint32_t records_per_block = 0U;
  uint32_t record_index = 0U;
  uint32_t cached_block_index = UINT32_MAX;
  uint32_t block_index = 0U;
  uint32_t offset_in_block = 0U;
  uint32_t stream_crc32 = 0xFFFFFFFFUL;
  uint32_t footer_crc32 = 0U;
  bool record_crc_valid = false;

  if (!app_emmc_log_lock()) {
    return false;
  }

  if (!app_emmc_log_ready) {
    app_emmc_log_unlock();
    return false;
  }

  if (!app_emmc_log_flush_locked()) {
    app_emmc_log_unlock();
    return false;
  }

  records_per_block = app_emmc_log_get_records_per_block();
  if (records_per_block == 0U) {
    app_emmc_log_unlock();
    return false;
  }

  memcpy(&header[0], app_emmc_log_dump_header_magic, sizeof(app_emmc_log_dump_header_magic));
  app_emmc_log_write_le16(&header[4], APP_EMMC_LOG_HEADER_VERSION);
  app_emmc_log_write_le16(&header[6], APP_EMMC_LOG_DUMP_HEADER_SIZE);
  app_emmc_log_write_le32(&header[8], app_emmc_log_session_id);
  app_emmc_log_write_le32(&header[12], app_emmc_log_record_count);
  app_emmc_log_write_le16(&header[16], APP_EMMC_LOG_DUMP_RECORD_SIZE);
  app_emmc_log_write_le16(&header[18], app_emmc_log_overflow ? 1U : 0U);
  app_emmc_log_write_le32(&header[20], app_emmc_log_crc32_compute(header, 20U));

  if (!app_emmc_log_uart_write_bytes(header, sizeof(header))) {
    app_emmc_log_unlock();
    return false;
  }

  for (record_index = 0U; record_index < app_emmc_log_record_count; record_index++) {
    block_index = record_index / records_per_block;
    offset_in_block = (record_index % records_per_block) * app_emmc_log_record_size;

    if (block_index != cached_block_index) {
      memset(&block, 0, sizeof(block));
      if (BSP_MMC_ReadBlocks(APP_EMMC_LOG_INSTANCE,
                             block.words,
                             APP_EMMC_LOG_START_BLOCK + 1U + block_index,
                             1U) != BSP_ERROR_NONE) {
        app_emmc_log_unlock();
        return false;
      }
      cached_block_index = block_index;
    }

    memcpy(&record, &block.bytes[offset_in_block], sizeof(record));
    record_crc_valid = app_emmc_log_record_crc_is_valid(&record);
    if (app_emmc_log_pack_dump_record(packed_record, &record, record_crc_valid) !=
        APP_EMMC_LOG_DUMP_RECORD_SIZE) {
      app_emmc_log_unlock();
      return false;
    }

    stream_crc32 =
        app_emmc_log_crc32_update(stream_crc32, packed_record, APP_EMMC_LOG_DUMP_RECORD_SIZE);
    if (!app_emmc_log_uart_write_bytes(packed_record, APP_EMMC_LOG_DUMP_RECORD_SIZE)) {
      app_emmc_log_unlock();
      return false;
    }
  }

  stream_crc32 ^= 0xFFFFFFFFUL;
  memcpy(&footer[0], app_emmc_log_dump_footer_magic, sizeof(app_emmc_log_dump_footer_magic));
  app_emmc_log_write_le32(&footer[4], app_emmc_log_record_count);
  app_emmc_log_write_le32(&footer[8], stream_crc32);
  footer_crc32 = app_emmc_log_crc32_compute(footer, 12U);
  app_emmc_log_write_le32(&footer[12], footer_crc32);

  if (!app_emmc_log_uart_write_bytes(footer, sizeof(footer))) {
    app_emmc_log_unlock();
    return false;
  }

  app_emmc_log_unlock();
  return true;
}
