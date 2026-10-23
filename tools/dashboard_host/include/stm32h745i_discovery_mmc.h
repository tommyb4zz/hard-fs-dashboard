/* Purpose: define the fake 64-block eMMC region used for host log/export checks. */
#ifndef DASHBOARD_HOST_MMC_H
#define DASHBOARD_HOST_MMC_H
#include "stm32h745i_discovery.h"
typedef struct {
  uint32_t LogBlockNbr;
} BSP_MMC_CardInfo;
/**
 * Initialize memory storage only.
 *
 * Input: instance must be zero. Returns -1 for another instance, otherwise erases all 64
 * memory blocks to 0xFF and returns BSP_ERROR_NONE. Reinitialization discards host
 * contents; it does not discover a real card.
 */
int32_t BSP_MMC_Init(uint32_t instance);
/**
 * Return the synthetic block count; this is not the board's actual eMMC capacity.
 *
 * Inputs: instance zero and writable info. Returns -1 for a wrong instance/null pointer;
 * on success writes total block count 4160 so production block 4096 has a 64-block region.
 * This deliberately small test capacity is not board capacity.
 */
int32_t BSP_MMC_GetCardInfo(uint32_t instance, BSP_MMC_CardInfo *info);
/**
 * Read a bounded synthetic storage range.
 *
 * Inputs: instance zero, writable bytes and bounded block/count. Returns -1 on invalid
 * range/pointer/instance, otherwise copies count*512 bytes from storage and returns
 * BSP_ERROR_NONE. There is no asynchronous DMA, cache maintenance or card-busy state.
 */
int32_t BSP_MMC_ReadBlocks(uint32_t instance, uint32_t *bytes, uint32_t block, uint32_t count);
/**
 * Write a bounded synthetic storage range.
 *
 * Inputs: instance zero, readable bytes and bounded block/count. Returns -1 on validation
 * failure, otherwise replaces count*512 storage bytes and returns BSP_ERROR_NONE. Data
 * persists only for this host process.
 */
int32_t BSP_MMC_WriteBlocks(uint32_t instance, uint32_t *bytes, uint32_t block, uint32_t count);
#endif
