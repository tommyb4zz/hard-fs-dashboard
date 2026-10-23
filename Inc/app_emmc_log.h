/**
 * File purpose: Expose logger readiness/overflow/session/record/block occupancy and the session,
 * append, flush and export API. A successful append can retain bytes only in RAM; flush
 * performs persistence. Status counts allocated physical data blocks separately from
 * unallocated staging. Calls can wait on the shared mutex and synchronous storage/UART
 * operations.
 */

#ifndef APP_EMMC_LOG_H
#define APP_EMMC_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_can.h"

/**
 * ready indicates successful current-session setup, overflow means reserved space exhausted.
 * record_count includes accepted staging; written_blocks counts allocated data blocks only,
 * reserved_blocks includes the header block, staged_bytes excludes an already-written tail.
 * Thus this status describes allocation, not a precise count of unpersisted dirty bytes.
 */
typedef struct {
  bool ready;
  bool overflow;
  uint32_t session_id;
  uint32_t record_count;
  uint32_t written_blocks;
  uint32_t reserved_blocks;
  uint32_t staged_bytes;
} app_emmc_log_status_t;

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
bool app_emmc_log_init(void);
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
bool app_emmc_log_append(uint32_t timestamp_ms, const app_can_dashboard_data_t *data);
/**
 * Take the mutex, flush staged eMMC records and release the mutex; return false if locking
 * or flushing fails, otherwise true.
 *
 * This public wrapper does not require caller mutex ownership. The locked helper can
 * rewrite a partial data tail or only retry dirty metadata; no staged data and clean
 * header succeed when ready. The returned boolean does not report how many records/bytes
 * were persisted.
 */
bool app_emmc_log_flush(void);
/**
 * Under the mutex, increment the session ID, discard staged records and reset counters by
 * writing a new header; update readiness and return the header write result.
 *
 * No arguments; use unsigned session_id+1 and replace logical history with a new empty
 * session. The reset does not erase all reserved blocks or preserve staged records. Lock
 * failure returns false without reset, while header failure has already cleared session
 * state and marks readiness false.
 */
bool app_emmc_log_reset_session(void);
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
void app_emmc_log_get_status(app_emmc_log_status_t *status);
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
void app_emmc_log_dump_csv(void);
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
bool app_emmc_log_dump_binary(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_EMMC_LOG_H */
