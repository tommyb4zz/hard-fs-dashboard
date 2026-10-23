/* Purpose: Declare the RAM-backed MMC interface exercised by production logger tests. */
#ifndef STORAGE_TEST_MMC_H
#define STORAGE_TEST_MMC_H
#include <stdint.h>
#define BSP_ERROR_NONE 0
typedef struct {
  uint32_t LogBlockNbr;
} BSP_MMC_CardInfo;
/**
 * Input: instance must be zero or assertion aborts. Returns BSP_ERROR_NONE; storage erase
 * is done by main rather than this stub, allowing explicit test setup.
 */
int32_t BSP_MMC_Init(uint32_t);
/**
 * Inputs: instance zero and writable info. Returns BSP_ERROR_NONE after publishing
 * card_blocks; instance mismatch aborts. The selected capacity constrains the production
 * logger's reserved region.
 */
int32_t BSP_MMC_GetCardInfo(uint32_t, BSP_MMC_CardInfo *);
/**
 * Inputs: instance zero, writable out and a block/count inside the test card region.
 * Invalid harness ranges assert; injected fail_read returns -1 without copying. Success
 * copies count*512 bytes and returns BSP_ERROR_NONE.
 */
int32_t BSP_MMC_ReadBlocks(uint32_t, uint32_t *, uint32_t, uint32_t);
/**
 * Inputs: instance zero, readable in and an asserted valid region. Returns -1 before
 * copying when header/data-specific failure is enabled, preserving old bytes. Success
 * copies blocks and returns BSP_ERROR_NONE, letting retries be checked against persisted
 * contents.
 */
int32_t BSP_MMC_WriteBlocks(uint32_t, uint32_t *, uint32_t, uint32_t);
#endif
