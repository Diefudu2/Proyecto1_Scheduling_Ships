#ifndef UTHREAD_H
#define UTHREAD_H

#include <ucontext.h>
#include <stddef.h>

/* =========================================================
 * uthread.h — Hilos en espacio de usuario
 * (v2: agrega UThreadSchedParams para el scheduler)
 * ========================================================= */

#define UTHREAD_STACK_SIZE (64 * 1024)   /* 64 KB por hilo */

/* ---------------------------------------------------------
 * Estados posibles de un UThread
 * --------------------------------------------------------- */
typedef enum {
    UTHREAD_READY   = 0,
    UTHREAD_RUNNING = 1,
    UTHREAD_BLOCKED = 2,   /* esperando entrar al canal          */
    UTHREAD_DONE    = 3
} UThreadState;


/* ---------------------------------------------------------
 * Parámetros de scheduling
 *
 * Llenados por ship.c al crear cada barco.
 * Leídos por scheduler.c para decidir el orden.
 * uthread.c no los interpreta — son opacos para él.
 * --------------------------------------------------------- */
typedef struct {
    int  priority;      /* PRIORITY: mayor número = más prioridad  */
    int  burst_ms;      /* SJF: tiempo estimado total de cruce     */
    int  remaining_ms;  /* STRN: tiempo restante (decrementado)    */
    int  deadline_ms;   /* EDF: deadline absoluto desde llegada    */
    long arrival_seq;   /* número de secuencia de llegada (FCFS)   */
} UThreadSchedParams;


/* ---------------------------------------------------------
 * Estructura principal de un hilo de usuario
 * --------------------------------------------------------- */
typedef struct UThread {
    ucontext_t          ctx;
    void               *stack;
    size_t              stack_size;
    UThreadState        state;

    void              (*func)(void *arg);
    void               *arg;

    int                 id;

    UThreadSchedParams  sched;        /* parámetros para el scheduler   */

    struct UThread     *next;
} UThread;


/* ---------------------------------------------------------
 * Globales compartidos con scheduler.c
 * --------------------------------------------------------- */
extern ucontext_t  g_sched_ctx;
extern UThread    *g_current;


/* ---------------------------------------------------------
 * API pública
 * --------------------------------------------------------- */
UThread *uthread_create(void (*func)(void *arg), void *arg,
                        size_t stack_size);
void     uthread_yield(void);
void     uthread_exit(void);
void     uthread_destroy(UThread *t);

#endif /* UTHREAD_H */