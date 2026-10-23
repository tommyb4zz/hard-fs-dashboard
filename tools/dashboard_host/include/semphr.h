/* Purpose: expose single-thread mutex stand-ins for host-only storage execution. */
#ifndef DASHBOARD_HOST_SEMPHR_H
#define DASHBOARD_HOST_SEMPHR_H
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
/**
 * Return a host mutex token; no FreeRTOS scheduler is emulated.
 *
 * Inputs: none. Returns the nonnull storage-array address as a token; allocates nothing
 * and establishes no exclusion. This is sufficient only for the sequential host runner.
 */
SemaphoreHandle_t xSemaphoreCreateMutex(void);
/**
 * Accept a valid token in this single-thread runner.
 *
 * Inputs: mutex is a token and ticks is ignored. Returns nonzero exactly when the token is
 * nonnull; never blocks or acquires scheduler state.
 */
int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t ticks);
/**
 * Release a valid host token.
 *
 * Input: mutex token. Returns nonzero for nonnull tokens; changes no lock/scheduling state
 * because every host command executes sequentially.
 */
int xSemaphoreGive(SemaphoreHandle_t mutex);
#endif
