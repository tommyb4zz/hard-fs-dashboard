/**
 * File purpose: Expose application task creation to main before scheduler startup. The implementation
 * initializes RAM/eMMC logs, creates one overwrite queue and GUI/CAN/console tasks;
 * false reports queue/task allocation failure without rolling back prior resources. Task
 * parameters, periods and placeholder inputs remain implementation details.
 */

#ifndef APP_TASKS_H
#define APP_TASKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * Initialize the loggers, allocate the one-element dashboard queue and create GUI, CAN and
 * console tasks; return false on queue/task allocation failure, otherwise true. eMMC
 * initialization failure does not stop task creation.
 *
 * Call before starting the scheduler. RAM initialization is unconditional and eMMC failure
 * is ignored; allocate a queue holding exactly one full snapshot, then GUI/CAN/console
 * tasks in that order. Stack sizes are FreeRTOS words (2048/1024/1024), not bytes. False
 * returns on the first allocation failure without deleting resources/tasks already
 * created. The function starts neither the scheduler nor a physical input binding.
 */
bool app_tasks_start(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TASKS_H */
