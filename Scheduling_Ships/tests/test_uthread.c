#include "uthread.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/*
 * Test aislado para la biblioteca uthread.
 *
 * No usa pthread.
 * No usa canal.
 * No usa GUI.
 * No usa scheduler.c.
 *
 * Implementa un mini-scheduler cooperativo tipo Round Robin
 * solo para comprobar que uthread_create(), uthread_yield()
 * y uthread_exit() funcionan correctamente.
 */

/* =========================================================
 * Parámetros manipulables del test
 * ========================================================= */

#define NUM_THREADS 3

/*
 * Cantidad de pasos que ejecuta cada UThread.
 * Puede modificar estos valores para probar hilos más largos
 * o más cortos.
 */
static int g_steps_per_thread[NUM_THREADS] = {
    5, 3, 4
};

/*
 * Si PRINT_VERBOSE = 1, se imprime cada paso.
 * Si PRINT_VERBOSE = 0, solo se imprime el resumen final.
 */
#define PRINT_VERBOSE 1

/*
 * Si YIELD_EVERY_STEP = 1, cada hilo cede CPU en cada paso.
 * Si YIELD_EVERY_STEP = 2, cede cada 2 pasos.
 * Si YIELD_EVERY_STEP = 3, cede cada 3 pasos.
 *
 * Esto permite probar el comportamiento cooperativo.
 */
#define YIELD_EVERY_STEP 1

/* =========================================================
 * Estructuras de prueba
 * ========================================================= */

typedef struct {
    int logical_id;
    int max_steps;
    int executed_steps;
} TestThreadArg;

/* Cola simple de UThreads listos */
typedef struct {
    UThread *head;
    UThread *tail;
    int count;
} TestReadyQueue;

static TestReadyQueue g_ready_queue = {0};

/* =========================================================
 * Funciones de cola
 * ========================================================= */

static void test_queue_push(TestReadyQueue *q, UThread *t)
{
    if (!t) return;

    t->next = NULL;

    if (q->tail) {
        q->tail->next = t;
    } else {
        q->head = t;
    }

    q->tail = t;
    q->count++;
}

static UThread *test_queue_pop(TestReadyQueue *q)
{
    if (!q->head) return NULL;

    UThread *t = q->head;
    q->head = t->next;

    if (!q->head) {
        q->tail = NULL;
    }

    t->next = NULL;
    q->count--;

    return t;
}

/* =========================================================
 * Función que ejecutará cada UThread
 * ========================================================= */

static void worker_func(void *arg)
{
    TestThreadArg *data = (TestThreadArg *)arg;

    for (int i = 0; i < data->max_steps; i++) {
        data->executed_steps++;

#if PRINT_VERBOSE
        printf("[UThread %d] paso %d/%d\n",
               data->logical_id,
               data->executed_steps,
               data->max_steps);
#endif

        /*
         * En un modelo cooperativo, el hilo decide cuándo
         * ceder el CPU. Aquí cedemos cada cierto número de pasos.
         */
        if ((i + 1) % YIELD_EVERY_STEP == 0) {
            uthread_yield();
        }
    }

#if PRINT_VERBOSE
    printf("[UThread %d] terminado\n", data->logical_id);
#endif

    /*
     * También podríamos no llamar uthread_exit() aquí,
     * porque uthread.c llama uthread_exit() automáticamente
     * cuando la función retorna mediante el trampoline.
     *
     * Pero lo llamamos explícitamente para probarlo.
     */
    uthread_exit();
}

/* =========================================================
 * Mini-scheduler cooperativo de prueba
 * ========================================================= */

static void run_test_scheduler(void)
{
    while (g_ready_queue.count > 0) {
        UThread *next = test_queue_pop(&g_ready_queue);
        assert(next != NULL);

        if (next->state == UTHREAD_DONE) {
            uthread_destroy(next);
            continue;
        }

        /*
         * Simulamos lo que haría un scheduler:
         * 1. Selecciona un hilo READY.
         * 2. Lo marca RUNNING.
         * 3. Lo pone como g_current.
         * 4. Cambia del contexto del scheduler al contexto del hilo.
         */
        g_current = next;
        g_current->state = UTHREAD_RUNNING;

#if PRINT_VERBOSE
        printf("Scheduler -> ejecutando UThread interno id=%d\n",
               g_current->id);
#endif

        swapcontext(&g_sched_ctx, &g_current->ctx);

        /*
         * Cuando el UThread llama uthread_yield() o uthread_exit(),
         * el control vuelve aquí.
         */

        if (g_current->state == UTHREAD_READY) {
            /*
             * El hilo cedió CPU voluntariamente.
             * Lo volvemos a meter al final de la cola.
             */
            test_queue_push(&g_ready_queue, g_current);
        } else if (g_current->state == UTHREAD_DONE) {
            /*
             * El hilo terminó. Liberamos su memoria.
             */
            uthread_destroy(g_current);
        } else {
            /*
             * Para esta prueba no esperamos BLOCKED.
             */
            fprintf(stderr,
                    "Estado inesperado en UThread id=%d: %d\n",
                    g_current->id,
                    g_current->state);
            exit(EXIT_FAILURE);
        }

        g_current = NULL;
    }
}

/* =========================================================
 * Main de prueba
 * ========================================================= */

int main(void)
{
    printf("=== TEST UTHREAD COOPERATIVO ===\n\n");

    TestThreadArg args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].logical_id = i + 1;
        args[i].max_steps = g_steps_per_thread[i];
        args[i].executed_steps = 0;

        UThread *t = uthread_create(worker_func,
                                    &args[i],
                                    UTHREAD_STACK_SIZE);

        if (!t) {
            fprintf(stderr, "Error creando UThread %d\n", i + 1);
            return EXIT_FAILURE;
        }

        test_queue_push(&g_ready_queue, t);
    }

    run_test_scheduler();

    printf("\n=== VERIFICACION FINAL ===\n");

    for (int i = 0; i < NUM_THREADS; i++) {
        printf("UThread %d: ejecutó %d/%d pasos\n",
               args[i].logical_id,
               args[i].executed_steps,
               args[i].max_steps);

        assert(args[i].executed_steps == args[i].max_steps);
    }

    printf("\nResultado: todas las UThreads terminaron correctamente.\n");
    printf("La prueba de create/yield/exit fue exitosa.\n");

    return EXIT_SUCCESS;
}