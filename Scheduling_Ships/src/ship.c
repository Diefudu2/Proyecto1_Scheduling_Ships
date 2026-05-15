#include "ship.h"
#include "canal.h"
#include "scheduler.h"
#include "uthread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

/* =========================================================
 * ship.c — Barcos implementados como UThreads
 *
 * Versión compatible con el canal.h nuevo:
 *
 *   Canal usa:
 *      c->config
 *      c->state
 *      c->mutex
 *
 *   Ship usa:
 *      s->dir
 *      s->pos
 *      s->canal_len
 *      s->speed
 *      s->uth
 *
 * Cada barco se ejecuta como UThread, no como pthread.
 * ========================================================= */

#define SHIP_TICK_MS 600

static int  g_ship_id_counter = 1;
static long g_arrival_seq     = 0;

extern Canal *g_canal;


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
 * Utilidades de tipo de barco
 * ========================================================= */

const char *ship_type_name(ShipType t)
{
    switch (t) {
        case SHIP_NORMAL: return "Normal";
        case SHIP_FISHER: return "Pesquero";
        case SHIP_PATROL: return "Patrulla";
    }

    return "Desconocido";
}

char ship_type_char(ShipType t)
{
    switch (t) {
        case SHIP_NORMAL: return 'N';
        case SHIP_FISHER: return 'F';
        case SHIP_PATROL: return 'P';
    }

    return '?';
}

int ship_default_speed(ShipType t)
{
    /*
     * Unidades por tick.
     * Normal es el más lento, patrulla el más rápido.
     */
    switch (t) {
        case SHIP_NORMAL: return 1;
        case SHIP_FISHER: return 2;
        case SHIP_PATROL: return 3;
    }

    return 1;
}

int ship_default_priority(ShipType t)
{
    switch (t) {
        case SHIP_NORMAL: return 1;
        case SHIP_FISHER: return 5;
        case SHIP_PATROL: return 10;
    }

    return 1;
}

int ship_default_burst(ShipType t, int canal_length)
{
    int speed = ship_default_speed(t);
    int ticks = (canal_length + speed - 1) / speed;

    return ticks * SHIP_TICK_MS;
}


/* =========================================================
 * Cola de barcos
 *
 * En el canal.h nuevo, el mutex está en Canal:
 *
 *      c->mutex
 *
 * Por eso estas funciones NO bloquean internamente.
 * Quien modifique las colas del canal debe tomar c->mutex.
 * ========================================================= */

void queue_push(ShipQueue *q, Ship *s)
{
    if (!q || !s) return;

    s->next = NULL;

    if (q->tail) {
        q->tail->next = s;
    } else {
        q->head = s;
    }

    q->tail = s;
    q->count++;
}

Ship *queue_pop(ShipQueue *q)
{
    if (!q || !q->head) return NULL;

    Ship *s = q->head;

    q->head = s->next;

    if (!q->head) {
        q->tail = NULL;
    }

    s->next = NULL;
    q->count--;

    return s;
}

void queue_remove(ShipQueue *q, Ship *target)
{
    if (!q || !target) return;

    Ship *prev = NULL;
    Ship *cur  = q->head;

    while (cur) {
        if (cur == target) {
            if (prev) {
                prev->next = cur->next;
            } else {
                q->head = cur->next;
            }

            if (q->tail == cur) {
                q->tail = prev;
            }

            cur->next = NULL;
            q->count--;
            return;
        }

        prev = cur;
        cur = cur->next;
    }
}


/* =========================================================
 * Helpers internos
 * ========================================================= */

static ShipQueue *ship_queue_for(Canal *c, ShipDir dir)
{
    if (!c) return NULL;

    return (dir == DIR_LEFT)
           ? &c->state.queue_left
           : &c->state.queue_right;
}

static void queue_push_for_scheduler(Canal *c, Ship *s)
{
    /*
     * Inserción visual/lógica según algoritmo.
     *
     * FCFS/RR:
     *   FIFO.
     *
     * PRIORITY:
     *   mayor priority primero.
     *
     * SJF:
     *   menor burst primero.
     *
     * STRN:
     *   menor remaining primero.
     *
     * EDF:
     *   menor deadline primero.
     */
    ShipQueue *q = ship_queue_for(c, s->dir);

    if (!q) return;

    if (!g_scheduler ||
        g_scheduler->algo == SS_FCFS ||
        g_scheduler->algo == SS_RR ||
        q->head == NULL) {
        queue_push(q, s);
        return;
    }

    Ship **cursor = &q->head;

    while (*cursor) {
        int goes_before = 0;

        switch (g_scheduler->algo) {
            case SS_PRIORITY:
                goes_before = s->priority > (*cursor)->priority;
                break;

            case SS_SJF:
                goes_before = s->burst_ms < (*cursor)->burst_ms;
                break;

            case SS_STRN:
                goes_before = s->uth &&
                              s->uth->sched.remaining_ms <
                              (*cursor)->uth->sched.remaining_ms;
                break;

            case SS_EDF:
                goes_before = s->deadline_ms < (*cursor)->deadline_ms;
                break;

            case SS_FCFS:
            case SS_RR:
            default:
                goes_before = 0;
                break;
        }

        if (goes_before) {
            break;
        }

        cursor = &(*cursor)->next;
    }

    s->next = *cursor;
    *cursor = s;

    if (s->next == NULL) {
        q->tail = s;
    }

    q->count++;
}


/* =========================================================
 * Bloqueo cooperativo
 *
 * Marca el UThread actual como BLOCKED y regresa al scheduler.
 * El canal deberá reactivarlo luego con sched_add().
 * ========================================================= */

static void uthread_block(void)
{
    assert(g_current != NULL);

    g_current->state = UTHREAD_BLOCKED;

    swapcontext(&g_current->ctx, &g_sched_ctx);
}


/* =========================================================
 * Espera cooperativa
 *
 * No usa usleep(). En vez de dormir el hilo real, cede CPU
 * repetidamente hasta que pasa el tiempo lógico.
 * ========================================================= */

static void ship_coop_delay_ms(Ship *s, int delay_ms)
{
    long long end = now_ms_local() + delay_ms;

    while (g_scheduler && g_scheduler->active && now_ms_local() < end) {
        if (s && s->uth) {
            s->uth->sched.remaining_ms =
                (s->uth->sched.remaining_ms > 0)
                ? s->uth->sched.remaining_ms
                : 0;
        }

        uthread_yield();
    }
}


/* =========================================================
 * Entrada al canal con modelo UThread
 * ========================================================= */

static int ship_try_enter_canal(Ship *s)
{
    if (!g_canal || !s) {
        return 0;
    }

    /*
     * Primero validar que este barco sea el primero de su cola.
     * Si no es el primero, no puede entrar aunque el canal esté libre.
     *
     * Esto evita que un barco nuevo se salte a barcos bloqueados
     * que ya estaban esperando.
     */
    pthread_mutex_lock(&g_canal->mutex);

    ShipQueue *q = ship_queue_for(g_canal, s->dir);

    if (!q || q->head != s) {
        pthread_mutex_unlock(&g_canal->mutex);
        return 0;
    }

    pthread_mutex_unlock(&g_canal->mutex);

    /*
     * Luego preguntar si el canal permite entrada.
     * canal_can_enter() toma y libera el mutex internamente.
     */
    if (!canal_can_enter(g_canal, s)) {
        return 0;
    }

    /*
     * Antes de entrar, verificar otra vez que siga siendo cabeza
     * de la cola. Luego se remueve solamente el primero.
     */
    pthread_mutex_lock(&g_canal->mutex);

    q = ship_queue_for(g_canal, s->dir);

    if (!q || q->head != s) {
        pthread_mutex_unlock(&g_canal->mutex);
        return 0;
    }

    queue_pop(q);

    pthread_mutex_unlock(&g_canal->mutex);

    /*
     * Registrar el barco dentro del canal.
     */
    canal_enter_immediate(g_canal, s);

    return 1;
}


/* =========================================================
 * ship_run
 *
 * Función que corre dentro del UThread del barco.
 * ========================================================= */

void ship_run(void *arg)
{
    Ship *s = (Ship *)arg;

    if (!s || !g_canal) {
        uthread_exit();
    }

    clock_gettime(CLOCK_MONOTONIC, &s->arrival_time);

    /*
     * Fase 1:
     * Esperar hasta poder entrar al canal.
     */
    s->state = SHIP_BLOCKED;

    while (g_scheduler && g_scheduler->active && !ship_try_enter_canal(s)) {
        uthread_block();
    }

    if (!g_scheduler || !g_scheduler->active) {
        s->state = SHIP_DONE;
        uthread_exit();
    }

    /*
     * Fase 2:
     * Cruzar el canal.
     */
    s->state = SHIP_CROSSING;
    clock_gettime(CLOCK_MONOTONIC, &s->start_time);

    if (s->dir == DIR_LEFT) {
        s->pos = 0;
    } else {
        s->pos = s->canal_len;
    }
    int crossed = 0;
    while (g_scheduler && g_scheduler->active) {
        int finished = 0;

        /*
         * Si una interrupcion expulso este barco del canal,
         * debe volver a esperar entrada.
         */
        if (s->state == SHIP_BLOCKED) {
            while (g_scheduler && g_scheduler->active && !ship_try_enter_canal(s)) {
                uthread_block();
            }

            if (!g_scheduler || !g_scheduler->active) {
                s->state = SHIP_DONE;
                uthread_exit();
            }

            s->state = SHIP_CROSSING;

            if (s->dir == DIR_LEFT) {
                s->pos = 0;
            } else {
                s->pos = s->canal_len;
            }

            continue;
        }

        /*
         * Avance segun direccion.
         */
        if (s->dir == DIR_LEFT) {
            s->pos += s->speed;

            if (s->pos >= s->canal_len) {
                s->pos = s->canal_len;
                finished = 1;
            }
        } else {
            s->pos -= s->speed;

            if (s->pos <= 0) {
                s->pos = 0;
                finished = 1;
            }
        }

        /*
         * Actualizar remaining_ms para STRN.
         */
        if (s->uth) {
            int remaining_units;

            if (s->dir == DIR_LEFT) {
                remaining_units = s->canal_len - s->pos;
            } else {
                remaining_units = s->pos;
            }

            if (remaining_units < 0) {
                remaining_units = 0;
            }

            int ticks_left = (remaining_units + s->speed - 1) / s->speed;
            s->uth->sched.remaining_ms = ticks_left * SHIP_TICK_MS;
        }

        if (finished) {
            crossed = 1;
            break;
        }

        /*
         * Espera cooperativa. Durante esta espera puede ocurrir
         * una interrupcion desde la GUI.
         */
        ship_coop_delay_ms(s, SHIP_TICK_MS);

        /*
         * Si la interrupcion ocurrio durante el delay cooperativo,
         * regresar al inicio del while para reentrar al canal.
         */
        if (s->state == SHIP_BLOCKED) {
            continue;
        }
    }

    /*
     * Fase 3:
     * Salir del canal.
     *
     * Esta parte debe estar FUERA del while de cruce.
     */
    /*
    * Solo cuenta como cruzado si llegó realmente al final.
    * Si el scheduler se apagó con w, no debe contar.
    */
    if (crossed && s->state == SHIP_CROSSING) {
        canal_exit(g_canal, s);
        clock_gettime(CLOCK_MONOTONIC, &s->finish_time);
    } else {
        /*
        * Salida por cierre/interrupción no completada.
        * No llamar canal_exit(), porque canal_exit() incrementa
        * las métricas de cruce.
        */
        s->state = SHIP_DONE;
    }

    uthread_exit();
}

static int ship_queue_is_full(Canal *c, ShipDir dir)
{
    if (!c) {
        return 1;
    }

    ShipQueue *q = ship_queue_for(c, dir);

    if (!q) {
        return 1;
    }

    /*
     * El enunciado pide maximo 4 barcos visibles/en cola.
     * Si config.queue_visible viene mal, forzamos el limite 4.
     */
    int limit = c->config.queue_visible;

    if (limit <= 0 || limit > 4) {
        limit = 4;
    }

    return q->count >= limit;
}

/* =========================================================
 * Crear barco
 * ========================================================= */

Ship *ship_create(ShipType type,
                  ShipDir dir,
                  int priority,
                  int burst_ms,
                  int deadline_ms)
{
    if (!g_canal) {
        fprintf(stderr, "[ERROR] ship_create: g_canal no inicializado\n");
        return NULL;
    }

    if (!g_scheduler) {
        fprintf(stderr, "[ERROR] ship_create: g_scheduler no inicializado\n");
        return NULL;
    }

    pthread_mutex_lock(&g_canal->mutex);

    if (ship_queue_is_full(g_canal, dir)) {
        pthread_mutex_unlock(&g_canal->mutex);

        fprintf(stderr,
                "[WARN] ship_create: cola %s llena, barco rechazado.\n",
                (dir == DIR_LEFT) ? "izquierda" : "derecha");

        return NULL;
    }

    pthread_mutex_unlock(&g_canal->mutex);

    Ship *s = calloc(1, sizeof(Ship));
    if (!s) {
        perror("ship_create: calloc");
        return NULL;
    }

    s->id    = g_ship_id_counter++;
    s->type  = type;
    s->dir   = dir;
    s->state = SHIP_WAITING;

    s->canal_len = g_canal->config.canal_length;
    s->speed     = ship_default_speed(type);
    s->pos       = (dir == DIR_LEFT) ? 0 : s->canal_len;

    s->priority = (priority > 0)
                  ? priority
                  : ship_default_priority(type);

    s->burst_ms = (burst_ms > 0)
                  ? burst_ms
                  : ship_default_burst(type, s->canal_len);

    if (deadline_ms > 0) {
        s->deadline_ms = deadline_ms;
    } else if (type == SHIP_PATROL) {
        s->deadline_ms = s->burst_ms;
    } else {
        s->deadline_ms = s->burst_ms * 4;
    }

    s->arrival_seq = g_arrival_seq++;
    s->next        = NULL;

    clock_gettime(CLOCK_MONOTONIC, &s->arrival_time);

    /*
     * Crear UThread asociado.
     */
    s->uth = uthread_create(ship_run, s, UTHREAD_STACK_SIZE);

    if (!s->uth) {
        fprintf(stderr,
                "[ERROR] ship_create: no se pudo crear UThread para barco %d\n",
                s->id);
        free(s);
        return NULL;
    }

    /*
     * Copiar parámetros al UThread para el scheduler.
     */
    s->uth->sched.priority     = s->priority;
    s->uth->sched.burst_ms     = s->burst_ms;
    s->uth->sched.remaining_ms = s->burst_ms;
    s->uth->sched.deadline_ms  = s->deadline_ms;
    s->uth->sched.arrival_seq  = s->arrival_seq;

    /*
     * Agregar a cola visual/lógica del lado correspondiente.
     */
    pthread_mutex_lock(&g_canal->mutex);
    queue_push_for_scheduler(g_canal, s);
    pthread_mutex_unlock(&g_canal->mutex);

    /*
     * Agregar a ready queue real del scheduler.
     */
    sched_add(g_scheduler, s->uth);

    return s;
}


/* =========================================================
 * Destruir barco
 * ========================================================= */

void ship_destroy(Ship *s)
{
    if (!s) return;

    /*
     * El UThread lo destruye el scheduler cuando queda DONE.
     * Aquí solo se libera la estructura Ship.
     */
    s->uth = NULL;
    free(s);
}