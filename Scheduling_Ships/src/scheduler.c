#include "scheduler.h"
#include "uthread.h"
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <sys/time.h>

/* =========================================================
 * scheduler.c — Scheduler de hilos en espacio de usuario
 *
 * Un solo loop central para todos los algoritmos.
 * La diferencia entre algoritmos está únicamente en
 * cómo se inserta en la ready queue (sched_add) y
 * el mecanismo de preemption (RR/STRN).
 * ========================================================= */


/* ---------------------------------------------------------
 * Global
 * --------------------------------------------------------- */
Scheduler *g_scheduler = NULL;


/* =========================================================
 * SECCIÓN 1 — Ready Queue (operaciones internas)
 *
 * Todas las funciones de cola asumen que el mutex ya
 * está tomado por el llamador.
 * ========================================================= */
static int queue_contains(ReadyQueue *q, UThread *t)
{
    if (!q || !t) {
        return 0;
    }

    UThread *cur = q->head;

    while (cur) {
        if (cur == t) {
            return 1;
        }

        cur = cur->next;
    }

    return 0;
}
/* Inserta al final — para FCFS y RR (orden de llegada) */
static void queue_push_fifo(ReadyQueue *q, UThread *t)
{
    t->next = NULL;
    if (q->tail)
        q->tail->next = t;
    else
        q->head = t;
    q->tail = t;
    q->count++;
}

/* Extrae desde la cabeza — todos los algoritmos */
static UThread *queue_pop_head(ReadyQueue *q)
{
    if (!q->head) return NULL;
    UThread *t  = q->head;
    q->head     = t->next;
    if (!q->head) q->tail = NULL;
    t->next     = NULL;
    q->count--;
    return t;
}

/*
 * Inserción ordenada genérica.
 *
 * Recibe una función de comparación:
 *   cmp(a, b) < 0  →  'a' va antes que 'b' (mayor prioridad)
 *
 * Ejemplos:
 *   PRIORITY : cmp = b.priority - a.priority  (desc)
 *   SJF      : cmp = a.burst_ms - b.burst_ms  (asc)
 *   STRN     : cmp = a.remaining_ms - b.remaining_ms (asc)
 *   EDF      : cmp = a.deadline_ms - b.deadline_ms   (asc)
 */
static void queue_push_sorted(ReadyQueue *q, UThread *t,
                              int (*cmp)(UThread *a, UThread *b))
{
    UThread **cursor = &q->head;

    /* avanzar mientras el nodo en cursor tiene mayor prioridad */
    while (*cursor && cmp(*cursor, t) <= 0)
        cursor = &(*cursor)->next;

    t->next = *cursor;
    if (!t->next) q->tail = t;
    *cursor = t;
    q->count++;
}

/* Comparadores */
static int cmp_priority(UThread *a, UThread *b)
{
    /* mayor priority primero → orden descendente */
    return b->sched.priority - a->sched.priority;
}

static int cmp_burst(UThread *a, UThread *b)
{
    return a->sched.burst_ms - b->sched.burst_ms;
}

static int cmp_remaining(UThread *a, UThread *b)
{
    return a->sched.remaining_ms - b->sched.remaining_ms;
}

static int cmp_deadline(UThread *a, UThread *b)
{
    return a->sched.deadline_ms - b->sched.deadline_ms;
}


/* =========================================================
 * SECCIÓN 2 — Preemption (RR con SIGALRM)
 *
 * Para RR usamos SIGALRM enviado específicamente al hilo
 * principal (pthread_kill). El handler interrumpe la
 * ejecución del UThread y hace swapcontext al scheduler.
 *
 * Requisito: sched_loop() debe correr en el main thread.
 * ========================================================= */

static void sigalrm_handler(int sig)
{
    (void)sig;
    Scheduler *s = g_scheduler;
    if (!s || !s->active) return;

    /* Solo preemptamos si hay un UThread corriendo y hay
     * alguien más esperando (si no, no tiene sentido)      */
    if (!g_current || g_current->state != UTHREAD_RUNNING)
        return;

    /* Re-encolar al hilo actual al final (comportamiento RR) */
    g_current->state = UTHREAD_READY;
    queue_push_fifo(&s->queue, g_current);   /* al final de la cola */
    s->metrics.total_preemptions++;
    s->preempt_flag = 1;

    /* Saltar al scheduler desde dentro del contexto del UThread.
     * Esto funciona porque SIGALRM se entrega al main thread,
     * que es el mismo que está ejecutando el UThread.         */
    swapcontext(&g_current->ctx, &g_sched_ctx);

    /* Cuando el scheduler vuelva a elegir este UThread,
     * la ejecución continúa acá, dentro del handler.
     * El handler retorna y el UThread sigue normalmente.    */
}

/* Arma el timer SIGALRM para que dispare en quantum_ms */
static void arm_timer(int quantum_ms)
{
    struct itimerval tv;
    tv.it_value.tv_sec     = quantum_ms / 1000;
    tv.it_value.tv_usec    = (quantum_ms % 1000) * 1000;
    tv.it_interval.tv_sec  = 0;
    tv.it_interval.tv_usec = 0;   /* one-shot, se reafirma en cada switch */
    setitimer(ITIMER_REAL, &tv, NULL);
}

/* Cancela el timer (cuando el scheduler está en idle) */
static void disarm_timer(void)
{
    struct itimerval tv = {0};
    setitimer(ITIMER_REAL, &tv, NULL);
}


/* =========================================================
 * SECCIÓN 3 — API pública
 * ========================================================= */

/* ---------------------------------------------------------
 * sched_init
 * --------------------------------------------------------- */
void sched_init(Scheduler *s, SchedAlgo algo, int quantum_ms)
{
    memset(s, 0, sizeof(*s));

    s->algo       = algo;
    s->quantum_ms = (quantum_ms > 0) ? quantum_ms : 100;
    s->active     = 1;

    pthread_mutex_init(&s->queue.lock, NULL);

    /* Guardar tid del hilo que inicializa (será el main thread) */
    s->main_thread = pthread_self();

    /* Instalar handler de SIGALRM para RR */
    if (algo == SS_RR) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = sigalrm_handler;
        sigemptyset(&sa.sa_mask);
        /* SA_RESTART: reiniciar syscalls interrumpidas (ncurses) */
        sa.sa_flags = SA_RESTART;
        sigaction(SIGALRM, &sa, NULL);
    }

    g_scheduler = s;
}


/* ---------------------------------------------------------
 * sched_add — insertar un UThread en la ready queue
 * --------------------------------------------------------- */
void sched_add(Scheduler *s, UThread *t)
{
        if (!s || !t) {
        return;
    }

    /*
     * Nunca reinsertar hilos terminados.
     */
    if (t->state == UTHREAD_DONE) {
        return;
    }

    /*
     * sched_add() solo debe recibir READY.
     */
    if (t->state != UTHREAD_READY) {
        return;
    }

    pthread_mutex_lock(&s->queue.lock);

    /*
     * Evitar duplicados en ready queue.
     * Esto previene que un UThread terminado pueda volver a ejecutarse
     * por haber quedado insertado más de una vez.
     */
    if (queue_contains(&s->queue, t)) {
        pthread_mutex_unlock(&s->queue.lock);
        return;
    }

    switch (s->algo) {

    
        case SS_FCFS:
        case SS_RR:
            queue_push_fifo(&s->queue, t);
            break;

        case SS_PRIORITY:
            queue_push_sorted(&s->queue, t, cmp_priority);
            break;

        case SS_SJF:
            queue_push_sorted(&s->queue, t, cmp_burst);
            break;

        case SS_STRN:
            queue_push_sorted(&s->queue, t, cmp_remaining);
            break;

        case SS_EDF:
            queue_push_sorted(&s->queue, t, cmp_deadline);
            break;
    }

    pthread_mutex_unlock(&s->queue.lock);
}


/* ---------------------------------------------------------
 * pick_next — extrae el siguiente UThread a ejecutar
 *
 * Para todos los algoritmos la extracción es la misma:
 * pop de la cabeza. La diferencia está en cómo se insertó.
 * --------------------------------------------------------- */
static UThread *pick_next(Scheduler *s)
{
    /* El mutex ya debe estar tomado por sched_loop() */
    return queue_pop_head(&s->queue);
}


/* ---------------------------------------------------------
 * sched_loop — loop principal
 *
 * DEBE correr en el main thread.
 * Bloquea hasta que s->active == 0.
 * --------------------------------------------------------- */
void sched_loop(Scheduler *s)
{
    /* Capturar el contexto del scheduler.
     * Cuando un UThread llame swapcontext(..., &g_sched_ctx),
     * la ejecución continúa justo después de este getcontext().
     *
     * Usamos g_sched_ctx directamente porque getcontext() +
     * swapcontext() necesitan que sea una variable de larga vida
     * (definida en uthread.c como global).                        */
    getcontext(&g_sched_ctx);

    while (s->active) {

        pthread_mutex_lock(&s->queue.lock);
        UThread *next = pick_next(s);
        pthread_mutex_unlock(&s->queue.lock);

        if (!next) {
            /* Ready queue vacía — idle */
            s->metrics.total_idle++;
            disarm_timer();
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 500000L;   /* 0.5 ms */
            nanosleep(&ts, NULL);
            continue;
        }

        /* Activar el hilo elegido */
        s->metrics.total_scheduled++;
        g_current     = next;
        next->state   = UTHREAD_RUNNING;
        s->preempt_flag = 0;

        /* Rearmar timer ANTES del swapcontext para RR */
        if (s->algo == SS_RR)
            arm_timer(s->quantum_ms);

        /* ── Cambio de contexto al UThread ──────────────────
         * El UThread corre desde acá hasta que:
         *   (a) llama uthread_yield()  → swapcontext vuelve acá
         *   (b) llama uthread_exit()   → swapcontext vuelve acá
         *   (c) SIGALRM lo interrumpe  → handler hace swapcontext
         *
         * En los tres casos, la ejecución continúa en la línea
         * siguiente a swapcontext() (el while del loop).
         * ────────────────────────────────────────────────── */
        swapcontext(&g_sched_ctx, &next->ctx);

        /* ── De vuelta en el scheduler ──────────────────── */

        /* Si el hilo terminó, liberar recursos */
        if (g_current && g_current->state == UTHREAD_DONE) {
            uthread_destroy(g_current);
            g_current = NULL;
            continue;
        }

        /* Para FCFS y SJF: si el hilo está READY (yield voluntario
         * pero no terminó), re-insertarlo.
         *
         * Para RR: el handler ya lo re-insertó en sigalrm_handler().
         * Para STRN: sched_add re-ordena en cada inserción.
         * Para PRIORITY/EDF: re-insertar en orden.                   */
        if (g_current && g_current->state == UTHREAD_READY) {
            pthread_mutex_lock(&s->queue.lock);
            switch (s->algo) {
                case SS_FCFS:
                    /* No preemptivo: al final para no romper orden */
                    queue_push_fifo(&s->queue, g_current);
                    break;
                case SS_RR:
                    /*
                    * Si el hilo volvió por SIGALRM, el handler ya lo reencoló.
                    * Si volvió por uthread_yield() voluntario, debemos reencolarlo aquí.
                    */
                    if (!s->preempt_flag)
                        queue_push_fifo(&s->queue, g_current);
                    break;
                case SS_PRIORITY:
                    queue_push_sorted(&s->queue, g_current, cmp_priority);
                    break;
                case SS_SJF:
                    /* No preemptivo: vuelve al final de su grupo */
                    queue_push_sorted(&s->queue, g_current, cmp_burst);
                    break;
                case SS_STRN:
                    queue_push_sorted(&s->queue, g_current, cmp_remaining);
                    break;
                case SS_EDF:
                    queue_push_sorted(&s->queue, g_current, cmp_deadline);
                    break;
            }
            pthread_mutex_unlock(&s->queue.lock);
        }

        /* Si el hilo está BLOCKED (esperando el canal), no re-insertar.
         * canal.c lo re-insertará vía sched_add() cuando pueda entrar. */

        g_current = NULL;
    }

    disarm_timer();
}


/* ---------------------------------------------------------
 * sched_shutdown
 * --------------------------------------------------------- */
void sched_shutdown(Scheduler *s)
{
    s->active = 0;
}


/* ---------------------------------------------------------
 * Helpers de parsing / display
 * --------------------------------------------------------- */
int sched_algo_from_str(const char *str)
{
    if (strcmp(str, "FCFS")     == 0) return SS_FCFS;
    if (strcmp(str, "RR")       == 0) return SS_RR;
    if (strcmp(str, "PRIORITY") == 0) return SS_PRIORITY;
    if (strcmp(str, "SJF")      == 0) return SS_SJF;
    if (strcmp(str, "STRN")     == 0) return SS_STRN;
    if (strcmp(str, "EDF")      == 0) return SS_EDF;
    return -1;
}

const char *sched_algo_name(SchedAlgo algo)
{
    switch (algo) {
        case SS_FCFS:     return "FCFS";
        case SS_RR:       return "RR";
        case SS_PRIORITY: return "PRIORITY";
        case SS_SJF:      return "SJF";
        case SS_STRN:     return "STRN";
        case SS_EDF:      return "EDF";
        default:             return "UNKNOWN";
    }
}