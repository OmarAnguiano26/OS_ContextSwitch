#include "tasks.h"
//#include "rtos_config.h"
#include "clock_config.h"
#include <queuelib.h>


/**********************************************************************************/
// Module defines
/**********************************************************************************/

QUEUE ready;

#define FORCE_INLINE 	__attribute__((always_inline)) inline

#define STACK_FRAME_SIZE			8
#define STACK_LR_OFFSET				2
#define STACK_PSR_OFFSET			1
#define STACK_PC_OFFSET				0 /*TODO Agregar el valor correcto*/
#define STACK_PSR_DEFAULT			0x01000000
#define MIN_PRIOR					-1
#define RTOS_TIC_PERIOD_IN_US 		(1000)
#define RTOS_STACK_SIZE				(400)
#define RTOS_MAX_NUMBER_OF_TASKS	(10)

typedef enum
{
	S_READY = 0,
	S_RUNNING,
	S_WAITING,
	S_SUSPENDED
} task_state_e;

typedef enum
{
	kFromISR = 0,
	kFromNormalExec
} task_switch_type_e;

/*Task Control Block*/
typedef struct
{
	uint8_t priority; /*TODO clean priority*/
	task_state_e state;
	uint32_t *sp;
	void (*task_body)();
	rtos_tick_t local_tick;
	uint32_t reserved[10];
	uint32_t stack[RTOS_STACK_SIZE];
} rtos_tcb_t;

struct
{
	uint8_t nTasks;
	rtos_task_handle_t current_task;
	rtos_task_handle_t next_task;
	rtos_tcb_t tasks[RTOS_MAX_NUMBER_OF_TASKS + 1];
	rtos_tick_t global_tick;
} task_list =
{ 0 };

static void reload_systick(void);
static void dispatcher(task_switch_type_e type);
static void rr_dispatcher(task_switch_type_e type);
static void activate_waiting_tasks();
FORCE_INLINE static void context_switch(task_switch_type_e type);
static void idle_task(void);

void rtos_start_scheduler(void)
{
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
	reload_systick();
	task_list.global_tick = ZERO;
	rtos_create_task(idle_task,ZERO,kAutoStart);
	for (;;);
}

rtos_task_handle_t rtos_create_task(void (*task_body)(), uint8_t priority, /*TODO clean priority*/
		rtos_autostart_e autostart)
{
	rtos_task_handle_t retval;
	if (RTOS_MAX_NUMBER_OF_TASKS > task_list.nTasks)/*Checks if it is possible to create more tasks*/
	{
		if(kStartSuspended == autostart)
		{
			task_list.tasks[task_list.nTasks].state = S_SUSPENDED;
		}
		else
		{
			task_list.tasks[task_list.nTasks].state = S_READY;
		}
		//task_list.tasks[task_list.nTasks].priority = priority;
		task_list.tasks[task_list.nTasks].local_tick = ZERO;
		task_list.tasks[task_list.nTasks].task_body = task_body;
		task_list.tasks[task_list.nTasks].sp = &(task_list.tasks[task_list.nTasks].stack[RTOS_STACK_SIZE -
																					STACK_FRAME_SIZE- ONE ]);
		task_list.tasks[task_list.nTasks].stack[RTOS_STACK_SIZE - STACK_LR_OFFSET] = (uint32_t)task_body;
		task_list.tasks[task_list.nTasks].stack[RTOS_STACK_SIZE - STACK_PSR_OFFSET] = STACK_PSR_DEFAULT;
		task_list.nTasks++;
		retval = task_list.nTasks;
	}
	else
	{
		retval = RTOS_INVALID_TASK;
	}

	return retval;
}

rtos_tick_t rtos_get_clock(void)
{
	return task_list.global_tick;
}

void rtos_delay(rtos_tick_t ticks)
{
	task_list.tasks[task_list.current_task].state = S_WAITING;
	task_list.tasks[task_list.current_task].local_tick = ticks;
	rr_dispatcher(kFromNormalExec);
}

void rtos_suspend_task(void)
{
	task_list.tasks[task_list.current_task].state = S_SUSPENDED;
	rr_dispatcher(kFromNormalExec);
}

void rtos_activate_task(rtos_task_handle_t task)
{
	task_list.tasks[task].state = S_READY;
	rr_dispatcher(kFromNormalExec);
}

/**********************************************************************************/
// Local methods implementation
/**********************************************************************************/

static void dispatcher(task_switch_type_e type)
{
	rtos_task_handle_t next_task = task_list.nTasks;
	int8_t high = MIN_PRIOR;
	uint8_t i;
	for(i = 0; i < task_list.nTasks; i++)
	{
		if( (high < task_list.tasks[i].priority) && (S_READY == task_list.tasks[i].state
			 || S_RUNNING == task_list.tasks[i].state) )
		{
			high = task_list.tasks[i].priority;
			next_task = i;
		}
	}
		task_list.next_task = next_task;
		if(task_list.current_task != next_task)
		{
			context_switch(type);
		}
}

static void rr_dispatcher(task_switch_type_e type)
{
	rtos_task_handle_t next_task = task_list.nTasks;
	// Find the next task that is ready to run
	do{
	    // Move to the next task in a circular manner
		next_task = (next_task + 1) % task_list.nTasks;

	    // Check if the next task is ready to run
		if(S_READY == task_list.tasks[next_task].state || S_RUNNING == task_list.tasks[next_task].state)
	    	{
	        	break; // Found a task that can run
	        }
	}while (next_task != task_list.current_task); // Loop until we reach back to the current task

	//Update the next_task in the task_list
	task_list.next_task = next_task;
	// Perform a context switch if the next task is different from the current task
	if (task_list.current_task != next_task)
	{
		context_switch(type);
	}
}

static void reload_systick(void)
{
	//SysTick->LOAD = USEC_TO_COUNT(RTOS_TIC_PERIOD_IN_US,
	      //  CLOCK_GetCoreSysClkFreq());
	SysTick->LOAD = RTOS_TIC_PERIOD_IN_US * CLOCK_GetCoreSysClkFreq() / 1000000U;
	SysTick->VAL = 0;
}

FORCE_INLINE static void context_switch(task_switch_type_e type)
{
	static uint8_t first = TRUE;
	register uint32_t r0 asm("r0");
	if(!first)
	{
		asm("mov r0, r7");
		task_list.tasks[task_list.current_task].sp = (uint32_t*) r0;
		if(type)
		{/**Normal execution*/
			task_list.tasks[task_list.current_task].sp = task_list.tasks[task_list.current_task].sp - 9;
		}
		else
		{/**From ISR*/
			task_list.tasks[task_list.current_task].sp = task_list.tasks[task_list.current_task].sp + 9;
		}
	}
	else
	{
		first = ZERO;
	}
	task_list.current_task = task_list.next_task;
	task_list.tasks[task_list.current_task].state = S_RUNNING;
	//task_list.current_task = task_list.next_task;
	SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

static void activate_waiting_tasks()
{
	uint8_t in;
	for(in = 0; in < task_list.nTasks; in++)
	{
		if(S_WAITING == task_list.tasks[in].state)
		{
			task_list.tasks[in].local_tick--;
			if(ZERO == task_list.tasks[in].local_tick)
			{
				task_list.tasks[in].state = S_READY;
			}
		}
	}
}

static void idle_task(void)
{
	for (;;)
	{

	}
}

void SysTick_Handler(void)
{
	task_list.global_tick++;
	activate_waiting_tasks();
	reload_systick();
	rr_dispatcher(kFromISR);
}

void PendSV_Handler(void)
{
	register uint32_t r0 asm("r0");
	(void)r0;
	SCB->ICSR |= SCB_ICSR_PENDSVCLR_Msk;
	r0 = (uint32_t)task_list.tasks[task_list.current_task].sp;
	asm("mov r7, r0");

}

