/* ============================================================
 * Archivo: thread.c
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Biblioteca de hilos sobre TAREAS REALES de FreeRTOS.
 *
 * MODELO (tareas reales calendarizadas por nosotros):
 * - thread_create() reserva un SimThread y lanza una TAREA REAL con
 *   xTaskCreate() cuyo cuerpo es thread_trampoline().
 * - thread_trampoline() es un bucle que:
 *      1) se BLOQUEA esperando una NOTIFICACIÓN de tarea hasta que el scheduler
 *         concede ejecución (mecanismo incorporado en la propia tarea);
 *      2) ejecuta UN paso de trabajo del barco (entry_step), que avanza el
 *         barco dentro del canal en el contexto de SU PROPIA tarea;
 *      3) avisa al dispatcher con g_step_done (handshake determinista).
 * - Así los barcos son procesos reales, pero el orden y el momento en que
 *   corren los sigue decidiendo nuestro scheduler (no FreeRTOS).
 *
 * SINCRONIZACIÓN:
 * - Concesión de ejecución: NOTIFICACIÓN de tarea (xTaskNotifyGive /
 *   ulTaskNotifyTake). NO se crea un semáforo por barco; cada tarea ya trae
 *   su notificación incorporada, así no se abusa de semáforos cuando hay N
 *   barcos.
 * - g_step_done: 1 semáforo binario GLOBAL -> fin-de-paso barco -> dispatcher
 *   (no escala con la cantidad de barcos).
 *
 * Convenciones:
 * - Las funciones públicas se declaran en el .h correspondiente.
 * - Las funciones static son utilidades internas del archivo.
 * - Retornos int usan 1=éxito/verdadero y 0=fallo/falso salvo que se indique otra cosa.
 * ============================================================ */
#include "thread.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

/* Parámetros de la tarea real de cada barco. */
#define THREAD_TASK_STACK_BYTES   3072
#define THREAD_TASK_PRIORITY      4     /* < prioridad del núcleo (5) */

static SimThread g_threads[MAX_SIM_THREADS];
static int g_thread_count = 0;
static int g_next_thread_id = 1;

/*
 * g_step_done:
 * Semáforo binario GLOBAL de handshake. La tarea del barco lo "da" al
 * terminar su paso de avance; el dispatcher (canal_drive_movement) lo "toma"
 * para avanzar al siguiente barco de forma determinista (frente-primero).
 * Se accede vía thread_take_step_done()/thread_drain_step_done() desde canal.c.
 */
static SemaphoreHandle_t g_step_done = NULL;

/* ============================================================
 * Acceso controlado a g_step_done para el dispatcher (canal.c)
 * ============================================================ */

SemaphoreHandle_t thread_step_done_handle(void)
{
    return g_step_done;
}

/* ============================================================
 * Cuerpo de la tarea real de cada barco
 * ============================================================ */

static void thread_trampoline(void *arg)
{
    SimThread *t = (SimThread *)arg;

    if (!t) {
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        /*
         * Esperar a que el scheduler nos conceda ejecución. Usamos la
         * notificación de tarea incorporada: ulTaskNotifyTake bloquea hasta
         * que el dispatcher llame xTaskNotifyGive sobre ESTA tarea. El primer
         * parámetro pdTRUE limpia la cuenta al recibirla (semántica binaria).
         */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (t->state == THREAD_DONE) {
            /* Aun así avisamos para no dejar al dispatcher esperando. */
            if (g_step_done) {
                xSemaphoreGive(g_step_done);
            }
            break;
        }

        /* Ejecutar UN paso de trabajo en el contexto de esta tarea real. */
        if (t->entry_step) {
            t->entry_step(t->arg);
        }

        /* Avisar fin-de-paso al dispatcher. */
        if (g_step_done) {
            xSemaphoreGive(g_step_done);
        }

        if (t->state == THREAD_DONE) {
            break;
        }
    }

    t->task = NULL;
    vTaskDelete(NULL);
}

/* ============================================================
 * API pública
 * ============================================================ */

void thread_lib_init(void)
{
    /*
     * Limpieza: si venimos de un RESET, hay tareas reales y semáforos vivos.
     * Se eliminan antes de reinicializar para no fugar recursos ni dejar
     * tareas huérfanas. thread_lib_init() solo lo llama la tarea del núcleo,
     * fuera de la ventana de movimiento, por lo que ninguna tarea de barco
     * está ejecutando un paso (ni reteniendo el mutex del canal) en ese momento.
     */
    for (int i = 0; i < MAX_SIM_THREADS; i++) {
        SimThread *t = &g_threads[i];

        if (t->task) {
            vTaskDelete(t->task);
            t->task = NULL;
        }
    }

    memset(g_threads, 0, sizeof(g_threads));
    g_thread_count = 0;
    g_next_thread_id = 1;

    if (g_step_done) {
        vSemaphoreDelete(g_step_done);
        g_step_done = NULL;
    }

    g_step_done = xSemaphoreCreateBinary();
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

            t->task = NULL;
            t->next = NULL;

            /* Lanzar la tarea real de FreeRTOS para este barco. */
            char name[16];
            snprintf(name, sizeof(name), "ship_%d", t->id);

            BaseType_t ok = xTaskCreate(
                thread_trampoline,
                name,
                THREAD_TASK_STACK_BYTES,
                t,
                THREAD_TASK_PRIORITY,
                &t->task
            );

            if (ok != pdPASS) {
                t->task = NULL;
                memset(t, 0, sizeof(*t));
                return NULL;
            }

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

void thread_grant_run(SimThread *t)
{
    if (t && t->task) {
        /* Despertar a ESTA tarea concreta mediante su notificación. */
        xTaskNotifyGive(t->task);
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