/* Purpose: control deterministic CAN queues, time and storage in the host runner. */
#ifndef DASHBOARD_FAKE_HAL_H
#define DASHBOARD_FAKE_HAL_H
#include <stdbool.h>
#include <stddef.h>
#include "stm32h7xx_hal.h"
#define DASHBOARD_HOST_RX_LIMIT 128U
typedef struct {
  FDCAN_TxHeaderTypeDef header;
  uint8_t data[8];
} dashboard_host_tx_t;
/**
 * Set the synthetic millisecond clock.
 *
 * Input: tick is an absolute unsigned millisecond value chosen by the command runner.
 * Returns nothing; replaces host_tick only. The fake imposes no monotonic rule; the T
 * command parser enforces it before calling.
 */
void dashboard_host_set_tick(uint32_t tick);
/**
 * Enqueue valid-length payload metadata; return false on bounded queue overflow.
 *
 * Inputs: id and metadata describe a synthetic frame; data contains size bytes and can be
 * null only for size zero. Returns false for unsupported lengths, full 128-entry batch or
 * absent payload; otherwise copies bytes/metadata into the next RX slot. Rejection by the
 * production protocol decoder happens later.
 */
bool dashboard_host_receive(
    uint32_t id, bool extended, bool remote, bool fd, const uint8_t *data, size_t size);
/**
 * Return the latest captured production transmit, clearing its pending flag.
 *
 * Input: tx is writable request storage. Returns false without consuming when tx is null
 * or no request is pending. On true it copies outgoing then clears tx_pending, making the
 * one-slot sink available for the next request.
 */
bool dashboard_host_take_tx(dashboard_host_tx_t *tx);
/**
 * Reset the memory sink before a binary dump.
 *
 * Inputs: none. Returns nothing; sets wire_size to zero before a dump so exports do not
 * append to an earlier export. The underlying array is not erased because only the newly
 * counted prefix is saved.
 */
void dashboard_host_export_begin(void);
/**
 * Persist the memory dump to an ordinary file; never open a UART.
 *
 * Input: path names an ordinary output file. Returns false on open/short-write/close
 * failure; writes the counted wire prefix in binary and closes even when fwrite is short.
 * The memory sink is retained for inspection/reuse; no CAN/UART device is opened.
 */
bool dashboard_host_export_save(const char *path);
#endif
