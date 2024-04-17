#ifndef __TASKS_H__
#define __TASKS_H__

void task_start();
int task_create(void (*run)(void *), void *userdata);
void task_kill(int task_id);
void task_self_terminal();

#endif
