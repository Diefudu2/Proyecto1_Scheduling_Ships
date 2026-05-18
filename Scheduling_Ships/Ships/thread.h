#ifndef THREAD_H
#define THREAD_H
#include "config.h"

#include <stdint.h>

#define MAX_SIM_THREADS CONFIG_MAX_SIM_THREADS

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_PREEMPTED,
    THREAD_PAUSED,
    THREAD_DONE
} ThreadState;

typedef struct SimThread {
    int id;
    ThreadState state;

    void (*entry_step)(void *arg);
    void *arg;

    int priority;
    int burst_ms;
    int remaining_ms;
    int deadline_ms;
    int quantum_used_ms;

    int pc;
    int saved_position;

    uint32_t arrival_tick;
    uint32_t start_tick;
    uint32_t finish_tick;

    struct SimThread *next;
} SimThread;

void thread_lib_init(void);

SimThread *thread_create(void (*entry_step)(void *arg),
                         void *arg,
                         int priority,
                         int burst_ms,
                         int deadline_ms);

void thread_set_ready(SimThread *t);
void thread_set_running(SimThread *t);
void thread_block(SimThread *t);
void thread_pause(SimThread *t);
void thread_preempt(SimThread *t);
void thread_exit(SimThread *t);

const char *thread_state_name(ThreadState state);

SimThread *thread_get_all(void);
int thread_get_count(void);

#endif