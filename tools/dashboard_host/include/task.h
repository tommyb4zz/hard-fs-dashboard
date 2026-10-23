/* Purpose: replace logger critical sections with no-ops in the single-thread host runner. */
#ifndef DASHBOARD_HOST_TASK_H
#define DASHBOARD_HOST_TASK_H
#include "FreeRTOS.h"
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#endif
