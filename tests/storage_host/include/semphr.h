/* Purpose: Replace the logger mutex with a single-threaded host-test stand-in. */
#ifndef STORAGE_TEST_SEMPHR_H
#define STORAGE_TEST_SEMPHR_H
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
/**
 * Return a non-null mutex token; the harness performs all operations sequentially.
 *
 * Inputs: none. Returns constant nonnull token (void*)1 without allocation; it does not
 * create a real FreeRTOS mutex.
 */
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
  return (void *)1;
}
/**
 * Accept the synthetic mutex without blocking because the harness has one thread.
 *
 * Inputs: mutex/ticks both ignored, including null and timeout values. Returns pdTRUE
 * unconditionally; no exclusion, scheduler or waiting behavior is tested by this storage
 * harness.
 */
static inline int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t ticks) {
  (void)mutex;
  (void)ticks;
  return pdTRUE;
}
/**
 * Release the synthetic mutex token without maintaining a scheduling state.
 *
 * Input: mutex ignored. Returns pdTRUE unconditionally and changes no synchronization
 * state; production mutex ordering is exercised sequentially only.
 */
static inline int xSemaphoreGive(SemaphoreHandle_t mutex) {
  (void)mutex;
  return pdTRUE;
}
#endif
