#include "thread.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static SimThread g_threads[MAX_SIM_THREADS];
static int g_thread_count = 0;
static int g_next_thread_id = 1;

void thread_lib_init(void)
{
    memset(g_threads, 0, sizeof(g_threads));
    g_thread_count = 0;
    g_next_thread_id = 1;
}

SimThread *thread_create(void (*entry_step)(void *arg),
                         void *arg,
                         int priority,
                         int burst_ms,
                         int deadline_ms)
{
    for (int i = 0; i < MAX_SIM_THREADS; i++) {
        if (g_threads[i].state == THREAD_UNUSED) {
            SimThread *t = &g_threads[i];

            memset(t, 0, sizeof(*t));

            t->id = g_next_thread_id++;
            t->state = THREAD_READY;
            t->entry_step = entry_step;
            t->arg = arg;

            t->priority = priority;
            t->burst_ms = burst_ms;
            t->remaining_ms = burst_ms;
            t->deadline_ms = deadline_ms;

            t->quantum_used_ms = 0;
            t->pc = 0;
            t->saved_position = -1;

            t->arrival_tick = (uint32_t)xTaskGetTickCount();
            t->start_tick = 0;
            t->finish_tick = 0;

            t->next = NULL;

            g_thread_count++;
            return t;
        }
    }

    return NULL;
}

void thread_set_ready(SimThread *t)
{
    if (t) {
        t->state = THREAD_READY;
    }
}

void thread_set_running(SimThread *t)
{
    if (t) {
        t->state = THREAD_RUNNING;
        if (t->start_tick == 0) {
            t->start_tick = (uint32_t)xTaskGetTickCount();
        }
    }
}

void thread_block(SimThread *t)
{
    if (t) {
        t->state = THREAD_BLOCKED;
    }
}

void thread_pause(SimThread *t)
{
    if (t) {
        t->state = THREAD_PAUSED;
    }
}

void thread_preempt(SimThread *t)
{
    if (t) {
        t->state = THREAD_PREEMPTED;
    }
}

void thread_exit(SimThread *t)
{
    if (t) {
        t->state = THREAD_DONE;
        t->finish_tick = (uint32_t)xTaskGetTickCount();
    }
}

const char *thread_state_name(ThreadState state)
{
    switch (state) {
        case THREAD_UNUSED:    return "UNUSED";
        case THREAD_READY:     return "READY";
        case THREAD_RUNNING:   return "RUNNING";
        case THREAD_BLOCKED:   return "BLOCKED";
        case THREAD_PREEMPTED: return "PREEMPTED";
        case THREAD_PAUSED:    return "PAUSED";
        case THREAD_DONE:      return "DONE";
        default:               return "UNKNOWN";
    }
}

SimThread *thread_get_all(void)
{
    return g_threads;
}

int thread_get_count(void)
{
    return g_thread_count;
}