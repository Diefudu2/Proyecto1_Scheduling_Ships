#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "uthread.h"
#include <pthread.h>
#include <signal.h>

/* =========================================================
 * scheduler.h — Scheduler de hilos en espacio de usuario
 *
 * Implementa los 6 algoritmos de scheduling como
 * variantes de pick_next(). El loop central es idéntico
 * para todos; solo cambia cómo se inserta y extrae de
 * la ready queue.
 * ========================================================= */


/* ---------------------------------------------------------
 * Algoritmos disponibles
 * --------------------------------------------------------- */
typedef enum {
    SS_FCFS     = 0,   /* First Come First Served             */
    SS_RR       = 1,   /* Round Robin (preemptivo, quantum)   */
    SS_PRIORITY = 2,   /* Priority (mayor número = más prio)  */
    SS_SJF      = 3,   /* Shortest Job First (no preemptivo)  */
    SS_STRN     = 4,   /* Shortest Time Remaining Next        */
    SS_EDF      = 5    /* Earliest Deadline First (tiempo real)*/
} SchedAlgo;


/* ---------------------------------------------------------
 * Ready queue — lista enlazada de UThreads listos
 *
 * El orden de inserción depende del algoritmo:
 *   FCFS/RR    → FIFO (insertar al final)
 *   PRIORITY   → ordenado por sched.priority DESC
 *   SJF        → ordenado por sched.burst_ms ASC
 *   STRN       → ordenado por sched.remaining_ms ASC
 *   EDF        → ordenado por sched.deadline_ms ASC
 *
 * La extracción siempre es desde la cabeza (O(1)).
 * --------------------------------------------------------- */
typedef struct {
    UThread        *head;
    UThread        *tail;
    int             count;
    pthread_mutex_t lock;   /* protege acceso desde canal threads */
} ReadyQueue;


/* ---------------------------------------------------------
 * Métricas del scheduler (para mostrar en GUI)
 * --------------------------------------------------------- */
typedef struct {
    long total_scheduled;   /* veces que pick_next() retornó algo */
    long total_preemptions; /* veces que se forzó un yield (RR/STRN)*/
    long total_idle;        /* veces que la ready queue estaba vacía */
} SchedMetrics;


/* ---------------------------------------------------------
 * Estructura principal del Scheduler
 * --------------------------------------------------------- */
typedef struct {
    ReadyQueue      queue;
    SchedAlgo       algo;
    int             quantum_ms;     /* quantum para RR                    */

    volatile int    active;         /* 1 = loop corriendo, 0 = shutdown   */
    volatile int    preempt_flag;   /* STRN: señal de preemption pendiente */

    pthread_t       main_thread;    /* tid del hilo que corre sched_loop() */

    SchedMetrics    metrics;
} Scheduler;


/* ---------------------------------------------------------
 * Global — accesible desde ship.c y canal.c
 * --------------------------------------------------------- */
extern Scheduler *g_scheduler;


/* ---------------------------------------------------------
 * API pública
 * --------------------------------------------------------- */

/*
 * sched_init — inicializa el scheduler
 *
 *   algo       : algoritmo a usar
 *   quantum_ms : quantum en ms (solo relevante para RR)
 *
 *   Debe llamarse antes de sched_add() o sched_loop().
 *   Configura SIGALRM si el algoritmo es RR.
 */
void sched_init(Scheduler *s, SchedAlgo algo, int quantum_ms);

/*
 * sched_add — agrega un UThread a la ready queue
 *
 *   Puede llamarse desde cualquier hilo (canal threads, main).
 *   Para STRN: si el nuevo hilo tiene menor remaining_ms que
 *   el hilo corriendo, dispara preemption inmediata.
 *
 *   El hilo debe estar en estado UTHREAD_READY.
 */
void sched_add(Scheduler *s, UThread *t);

/*
 * sched_loop — loop principal del scheduler
 *
 *   Bloquea hasta que s->active == 0.
 *   Debe correrse en el hilo principal (main thread).
 *   Llama pick_next() y hace swapcontext al hilo elegido.
 */
void sched_loop(Scheduler *s);

/*
 * sched_shutdown — detiene el loop limpiamente
 *
 *   Puede llamarse desde cualquier hilo.
 *   El loop termina en la próxima iteración.
 */
void sched_shutdown(Scheduler *s);

/*
 * sched_algo_from_str — parsea el nombre del algoritmo
 *
 *   Útil para leer desde canal.cfg.
 *   "FCFS" → SS_FCFS, "RR" → SS_RR, etc.
 *   Retorna -1 si el string no es válido.
 */
int sched_algo_from_str(const char *str);

/*
 * sched_algo_name — retorna el nombre del algoritmo
 */
const char *sched_algo_name(SchedAlgo algo);

#endif /* SCHEDULER_H */