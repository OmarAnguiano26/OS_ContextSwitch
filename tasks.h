#ifndef __TASKS_H__
#define __TASKS_H__

#include "stdint.h"
#define RTOS_INVALID_TASK	(-1)
#define ZERO 				(0U)
#define TRUE				(1U)
#define ONE					(1U)
#define STACK_POINTER_SET	(9U)


/*Task state type */
typedef enum
{
	kAutoStart, kStartSuspended
} rtos_autostart_e;

/*Task handle type, used to identify a task */
typedef int8_t rtos_task_handle_t;

/*Tick type, used for time measurement */
typedef uint64_t rtos_tick_t;

/** Starts the scheduler*/
void rtos_start_scheduler(void);

/**Create task*/
rtos_task_handle_t rtos_create_task(void (*task_body)(), uint8_t priority,
        rtos_autostart_e autostart);

/**Suspends the task*/
void rtos_suspend_task(void);

/**Activates the task*/
void rtos_activate_task(rtos_task_handle_t task);

/*
 * @brief Returns the global tick
 */
rtos_tick_t rtos_get_clock(void);

/*
 *  Suspends the task calling this function by a certain
 * amount of time
 *
 * @param ticks amount of ticks for the delay
 * @retval none
 */
void rtos_delay(rtos_tick_t ticks);

#endif
