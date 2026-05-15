#include "scheduler.h"
#include "uthread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

/*
 * Test específico para Round Robin con quantum.
 *
 * Este test usa:
 *   - scheduler.c
 *   - uthread.c
 *
 * No usa:
 *   - ship.c
 *   - canal.c
 *   - gui.c
 *
 * Objetivo:
 *   Verificar que RR interrumpe UThreads que NO llaman yield()
 *   voluntariamente, usando el timer SIGALRM del scheduler.
 */

#define NUM_TASKS       3
#define QUANTUM_MS      40
#define WORK_MS         220
#define MAX_SEGMENTS    128

typedef struct {
    int logical_id;
    int ran;
    int finished;
    long checkpoints;
} RRTask;

static Scheduler g_sched;
static RRTask g_tasks[NUM_TASKS];

static int g_finished_count = 0;

/*
 * Secuencia de segmentos observada.
 *
 * Ejemplo esperado aproximado:
 *   1 -> 2 -> 3 -> 1 -> 2 -> 3 -> ...
 *
 * No exigimos una secuencia exacta porque depende del temporizador,
 * del sistema operativo y de la carga de la máquina.
 */
static int g_segments[MAX_SEGMENTS];
static int g_segment_count = 0;
static int g_last_segment = -1;

/* =========================================================
 * Utilidades de tiempo
 * ========================================================= */

static long long now_ms_local(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

/* =========================================================
 * Registro de segmentos de ejecución
 * ========================================================= */

static void record_segment(int logical_id)
{
    /*
     * Si el último segmento registrado ya era de esta tarea,
     * no registramos otra vez. Solo registramos cuando cambia
     * la tarea en ejecución.
     */
    if (g_last_segment == logical_id) {
        return;
    }

    if (g_segment_count < MAX_SEGMENTS) {
        g_segments[g_segment_count++] = logical_id;
    }

    g_last_segment = logical_id;
}

/* =========================================================
 * Función de cada UThread
 * ========================================================= */

static void rr_task_func(void *arg)
{
    RRTask *task = (RRTask *)arg;

    task->ran++;

    long long start = now_ms_local();

    /*
     * Trabajo ocupado durante WORK_MS.
     *
     * Importante:
     *   Aquí NO llamamos uthread_yield().
     *
     * Si RR con quantum funciona, el scheduler debería
     * interrumpir esta función con SIGALRM y darle turno
     * a otro UThread.
     */
    while ((now_ms_local() - start) < WORK_MS) {
        task->checkpoints++;

        /*
         * Este registro permite ver cuándo una tarea vuelve
         * a ejecutarse después de que otra recibió CPU.
         */
        if ((task->checkpoints % 5000) == 0) {
            record_segment(task->logical_id);
        }
    }

    record_segment(task->logical_id);

    task->finished = 1;
    g_finished_count++;

    printf("Tarea %d terminada | checkpoints=%ld\n",
           task->logical_id,
           task->checkpoints);

    /*
     * Cuando termina la última tarea, apagamos el scheduler.
     */
    if (g_finished_count == NUM_TASKS) {
        sched_shutdown(g_scheduler);
    }

    uthread_exit();
}

/* =========================================================
 * Crear tareas
 * ========================================================= */

static void add_rr_tasks(Scheduler *s)
{
    for (int i = 0; i < NUM_TASKS; i++) {
        g_tasks[i].logical_id = i + 1;
        g_tasks[i].ran = 0;
        g_tasks[i].finished = 0;
        g_tasks[i].checkpoints = 0;

        UThread *t = uthread_create(rr_task_func,
                                    &g_tasks[i],
                                    UTHREAD_STACK_SIZE);

        if (!t) {
            fprintf(stderr, "ERROR: no se pudo crear UThread %d\n", i + 1);
            exit(EXIT_FAILURE);
        }

        /*
         * Para RR estos valores no afectan el orden inicial,
         * pero los llenamos para mantener consistencia con
         * la estructura de scheduling.
         */
        t->sched.priority     = 1;
        t->sched.burst_ms     = WORK_MS;
        t->sched.remaining_ms = WORK_MS;
        t->sched.deadline_ms  = 0;
        t->sched.arrival_seq  = i;

        sched_add(s, t);
    }
}

/* =========================================================
 * Impresión de resultados
 * ========================================================= */

static void print_segments(void)
{
    printf("\nSegmentos observados:\n  ");

    for (int i = 0; i < g_segment_count; i++) {
        printf("%d", g_segments[i]);

        if (i < g_segment_count - 1) {
            printf(" -> ");
        }
    }

    printf("\n");
}

static int count_distinct_tasks_in_segments(void)
{
    int seen[NUM_TASKS + 1];
    memset(seen, 0, sizeof(seen));

    for (int i = 0; i < g_segment_count; i++) {
        int id = g_segments[i];

        if (id >= 1 && id <= NUM_TASKS) {
            seen[id] = 1;
        }
    }

    int count = 0;

    for (int i = 1; i <= NUM_TASKS; i++) {
        count += seen[i];
    }

    return count;
}

/* =========================================================
 * Main
 * ========================================================= */

int main(void)
{
    printf("=== TEST RR CON QUANTUM ===\n");
    printf("NUM_TASKS  = %d\n", NUM_TASKS);
    printf("QUANTUM_MS = %d\n", QUANTUM_MS);
    printf("WORK_MS    = %d\n\n", WORK_MS);

    sched_init(&g_sched, SS_RR, QUANTUM_MS);

    add_rr_tasks(&g_sched);

    sched_loop(&g_sched);

    print_segments();

    printf("\nMetricas del scheduler:\n");
    printf("  total_scheduled   = %ld\n", g_sched.metrics.total_scheduled);
    printf("  total_preemptions = %ld\n", g_sched.metrics.total_preemptions);
    printf("  total_idle        = %ld\n", g_sched.metrics.total_idle);

    printf("\nVerificacion por tarea:\n");

    for (int i = 0; i < NUM_TASKS; i++) {
        printf("  Tarea %d | ran=%d | finished=%d | checkpoints=%ld\n",
               g_tasks[i].logical_id,
               g_tasks[i].ran,
               g_tasks[i].finished,
               g_tasks[i].checkpoints);

        assert(g_tasks[i].ran == 1);
        assert(g_tasks[i].finished == 1);
        assert(g_tasks[i].checkpoints > 0);
    }

    /*
     * Verificaciones importantes:
     *
     * 1. Debe haber más segmentos que tareas.
     *    Si solo hubiera 1 -> 2 -> 3, no hubo preemption real.
     *
     * 2. Debe haber preemptions registradas por el scheduler.
     *
     * 3. Todas las tareas deben aparecer en los segmentos.
     */
    assert(g_segment_count > NUM_TASKS);
    assert(g_sched.metrics.total_preemptions > 0);
    assert(count_distinct_tasks_in_segments() == NUM_TASKS);

    printf("\nResultado: RR con quantum produjo preemption correctamente.\n");

    return EXIT_SUCCESS;
}