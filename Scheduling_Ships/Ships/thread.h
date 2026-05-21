#ifndef THREAD_H
#define THREAD_H

/* ============================================================
 * Archivo: thread.h
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Define SimThread, estados de hilo y la API de la biblioteca de hilos.
 *
 * CAMBIO DE FASE (tareas reales):
 * - Cada SimThread ahora envuelve una TAREA REAL de FreeRTOS (xTaskCreate).
 * - La tarea NO corre libre: se bloquea en un semáforo de compuerta (run_gate)
 *   hasta que NUESTRO scheduler le concede ejecución (thread_grant_run).
 *   Así los barcos son procesos reales, pero seguimos calendarizándolos.
 *
 * Este encabezado contiene la API pública del módulo. Mantener aquí solo
 * tipos, constantes y prototipos requeridos por otros archivos.
 * ============================================================ */
#include "config.h"

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

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

    /* ---- Soporte de tarea real de FreeRTOS ---- */
    TaskHandle_t    task;       /* tarea real asociada a este hilo */
    SemaphoreHandle_t run_gate; /* compuerta: el scheduler concede ejecución */

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

/*
 * thread_grant_run:
 * Libera la compuerta run_gate del hilo indicado. Es el mecanismo con el que
 * el scheduler/dispatcher concede UN paso de ejecución a la tarea real del
 * barco. Sin esta concesión la tarea permanece bloqueada.
 */
void thread_grant_run(SimThread *t);

/*
 * thread_step_done_handle:
 * Devuelve el semáforo binario global de handshake "fin-de-paso". Lo usa el
 * dispatcher del canal para esperar a que la tarea del barco termine su avance.
 */
SemaphoreHandle_t thread_step_done_handle(void);

const char *thread_state_name(ThreadState state);

SimThread *thread_get_all(void);
int thread_get_count(void);

#endif