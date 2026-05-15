#include "scheduler.h"
#include "uthread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * Test aislado del scheduler real.
 *
 * Este test usa:
 *   - scheduler.c
 *   - uthread.c
 *
 * No usa:
 *   - ship.c
 *   - canal.c
 *   - gui.c
 *   - hardware
 *
 * Objetivo:
 *   Verificar que sched_init(), sched_add() y sched_loop()
 *   ordenan y ejecutan UThreads según el algoritmo seleccionado.
 */

#define NUM_TASKS 4
#define NUM_CASES 6
#define QUANTUM_MS 100

/*
 * Cambiar VERBOSE a 0 si se quiere ver solo el resumen.
 */
#define VERBOSE 1

typedef struct {
    int logical_id;
    int priority;
    int burst_ms;
    int remaining_ms;
    int deadline_ms;
    int ran;
} TestTask;

typedef struct {
    SchedAlgo algo;
    const char *name;
    int expected[NUM_TASKS];
} TestCase;

static Scheduler g_sched;

static TestTask g_tasks[NUM_TASKS];

static int g_run_order[NUM_TASKS];
static int g_run_count = 0;
static int g_finished_count = 0;

/* =========================================================
 * Carga artificial de tareas
 * ========================================================= */

static void reset_tasks(void)
{
    /*
     * Estas tareas simulan barcos, pero sin canal.
     *
     * Tarea 1:
     *   prioridad baja, burst alto, deadline lejano.
     *
     * Tarea 2:
     *   prioridad alta, burst medio, deadline más cercano.
     *
     * Tarea 3:
     *   prioridad media, burst más corto.
     *
     * Tarea 4:
     *   prioridad media-alta, burst intermedio, deadline intermedio.
     */
    g_tasks[0] = (TestTask){
        .logical_id = 1,
        .priority = 1,
        .burst_ms = 500,
        .remaining_ms = 500,
        .deadline_ms = 4000,
        .ran = 0
    };

    g_tasks[1] = (TestTask){
        .logical_id = 2,
        .priority = 10,
        .burst_ms = 200,
        .remaining_ms = 200,
        .deadline_ms = 1000,
        .ran = 0
    };

    g_tasks[2] = (TestTask){
        .logical_id = 3,
        .priority = 5,
        .burst_ms = 100,
        .remaining_ms = 100,
        .deadline_ms = 3000,
        .ran = 0
    };

    g_tasks[3] = (TestTask){
        .logical_id = 4,
        .priority = 7,
        .burst_ms = 350,
        .remaining_ms = 350,
        .deadline_ms = 2000,
        .ran = 0
    };

    memset(g_run_order, 0, sizeof(g_run_order));
    g_run_count = 0;
    g_finished_count = 0;
}

/* =========================================================
 * Función ejecutada por cada UThread
 * ========================================================= */

static void task_func(void *arg)
{
    TestTask *task = (TestTask *)arg;

    task->ran++;

    if (g_run_count < NUM_TASKS) {
        g_run_order[g_run_count] = task->logical_id;
        g_run_count++;
    }

#if VERBOSE
    printf("  Ejecutando tarea logica=%d | priority=%d | burst=%d | remaining=%d | deadline=%d\n",
           task->logical_id,
           task->priority,
           task->burst_ms,
           task->remaining_ms,
           task->deadline_ms);
#endif

    g_finished_count++;

    /*
     * Cuando termina la última tarea, apagamos el scheduler.
     * Si no hacemos esto, sched_loop() queda esperando más trabajo.
     */
    if (g_finished_count == NUM_TASKS) {
        sched_shutdown(g_scheduler);
    }

    uthread_exit();
}

/* =========================================================
 * Crear UThreads y agregarlos al scheduler real
 * ========================================================= */

static void add_tasks_to_scheduler(Scheduler *s)
{
    for (int i = 0; i < NUM_TASKS; i++) {
        UThread *t = uthread_create(task_func,
                                    &g_tasks[i],
                                    UTHREAD_STACK_SIZE);

        if (!t) {
            fprintf(stderr,
                    "ERROR: no se pudo crear UThread para tarea %d\n",
                    g_tasks[i].logical_id);
            exit(EXIT_FAILURE);
        }

        /*
         * Estos son los campos que usa scheduler.c para ordenar.
         */
        t->sched.priority     = g_tasks[i].priority;
        t->sched.burst_ms     = g_tasks[i].burst_ms;
        t->sched.remaining_ms = g_tasks[i].remaining_ms;
        t->sched.deadline_ms  = g_tasks[i].deadline_ms;
        t->sched.arrival_seq  = i;

        sched_add(s, t);
    }
}

/* =========================================================
 * Utilidades de verificación
 * ========================================================= */

static void print_order(const char *label, const int order[NUM_TASKS])
{
    printf("%s", label);

    for (int i = 0; i < NUM_TASKS; i++) {
        printf("%d", order[i]);

        if (i < NUM_TASKS - 1) {
            printf(" -> ");
        }
    }

    printf("\n");
}

static int order_matches(const int expected[NUM_TASKS])
{
    for (int i = 0; i < NUM_TASKS; i++) {
        if (g_run_order[i] != expected[i]) {
            return 0;
        }
    }

    return 1;
}

/* =========================================================
 * Ejecutar un caso de prueba
 * ========================================================= */

static void run_case(const TestCase *tc)
{
    printf("\n=== TEST SCHEDULER: %s ===\n", tc->name);

    reset_tasks();

    sched_init(&g_sched, tc->algo, QUANTUM_MS);

    add_tasks_to_scheduler(&g_sched);

    sched_loop(&g_sched);

    print_order("  Orden obtenido : ", g_run_order);
    print_order("  Orden esperado : ", tc->expected);

    for (int i = 0; i < NUM_TASKS; i++) {
        assert(g_tasks[i].ran == 1);
    }

    if (!order_matches(tc->expected)) {
        fprintf(stderr,
                "\nERROR: el algoritmo %s no produjo el orden esperado.\n",
                tc->name);
        exit(EXIT_FAILURE);
    }

    printf("  Resultado: OK\n");
}

/* =========================================================
 * Main
 * ========================================================= */

int main(void)
{
    /*
     * Orden esperado según los datos de reset_tasks().
     *
     * FCFS:
     *   Respeta orden de llegada.
     *   Esperado: 1, 2, 3, 4.
     *
     * RR:
     *   En esta prueba las tareas terminan en una sola corrida,
     *   por eso se comporta como FIFO.
     *   Esperado: 1, 2, 3, 4.
     *
     * PRIORITY:
     *   Mayor priority primero.
     *   T2=10, T4=7, T3=5, T1=1.
     *   Esperado: 2, 4, 3, 1.
     *
     * SJF:
     *   Menor burst_ms primero.
     *   T3=100, T2=200, T4=350, T1=500.
     *   Esperado: 3, 2, 4, 1.
     *
     * STRN:
     *   Menor remaining_ms primero.
     *   T3=100, T2=200, T4=350, T1=500.
     *   Esperado: 3, 2, 4, 1.
     *
     * EDF:
     *   Menor deadline_ms primero.
     *   T2=1000, T4=2000, T3=3000, T1=4000.
     *   Esperado: 2, 4, 3, 1.
     */
    TestCase cases[NUM_CASES] = {
        { SS_FCFS,     "FCFS",     {1, 2, 3, 4} },
        { SS_RR,       "RR",       {1, 2, 3, 4} },
        { SS_PRIORITY, "PRIORITY", {2, 4, 3, 1} },
        { SS_SJF,      "SJF",      {3, 2, 4, 1} },
        { SS_STRN,     "STRN",     {3, 2, 4, 1} },
        { SS_EDF,      "EDF",      {2, 4, 3, 1} }
    };

    printf("=== TEST DE SCHEDULER CON UTHREADS ARTIFICIALES ===\n");
    printf("Este test valida el orden de seleccion de la ready queue.\n");

    for (int i = 0; i < NUM_CASES; i++) {
        run_case(&cases[i]);
    }

    printf("\n=== VERIFICACION FINAL ===\n");
    printf("Todos los algoritmos produjeron el orden esperado para esta carga.\n");
    printf("La prueba de sched_init/sched_add/sched_loop fue exitosa.\n");

    return EXIT_SUCCESS;
}