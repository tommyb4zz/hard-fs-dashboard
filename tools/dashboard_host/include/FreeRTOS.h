/* Purpose: define the minimal single-thread host boundary used by the production loggers. */
#ifndef DASHBOARD_HOST_FREERTOS_H
#define DASHBOARD_HOST_FREERTOS_H
#include <stdint.h>
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdFALSE 0
#endif
