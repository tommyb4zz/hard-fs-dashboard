/**
 * File purpose: Expose application task creation to main before scheduler startup. The
 * implementation initializes RAM/eMMC logs, creates one overwrite queue and GUI/CAN/console tasks;
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
 * Start live CAN tasks by default, or an explicit synthetic demo when demo_mode is true.
 * Demo skips eMMC initialization, journal attachment and the CAN task. Return false on
 * queue/task allocation failure. eMMC failure does not stop live task creation.
 *
 * Call before starting the scheduler. RAM initialization is unconditional and eMMC failure
 * is ignored; allocate a queue holding exactly one full snapshot, then GUI/CAN/console
 * tasks in that order. Stack sizes are FreeRTOS words (2048/1024/1024), not bytes. False
 * returns on the first allocation failure without deleting resources/tasks already
 * created. The function starts neither the scheduler nor a physical input binding.
 */
bool app_tasks_start(bool demo_mode);

#ifdef __cplusplus
}
#endif

#endif /* APP_TASKS_H */
