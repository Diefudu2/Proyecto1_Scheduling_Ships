#ifndef SIM_SEMAPHORE_H
#define SIM_SEMAPHORE_H

#include "thread.h"

typedef struct SimSemaphore {
    int value;

    SimThread *blocked_head;
    SimThread *blocked_tail;
} SimSemaphore;

void sim_sem_init(SimSemaphore *sem, int initial_value);

int sim_sem_wait(SimSemaphore *sem, SimThread *thread);
SimThread *sim_sem_signal(SimSemaphore *sem);

int sim_sem_value(SimSemaphore *sem);

#endif