#include "ship.h"
#include "canal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static int             g_ship_id_counter = 1;
static pthread_mutex_t g_id_mutex        = PTHREAD_MUTEX_INITIALIZER;

extern Canal *g_canal;

/* ── Tiempo actual en ms ─────────────────────────────────────── */
long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

/* ── Nombres y valores por tipo ──────────────────────────────── */
const char *ship_type_name(ShipType t) {
    switch (t) {
        case SHIP_NORMAL:  return "Normal";
        case SHIP_FISHING: return "Pesquero";
        case SHIP_PATROL:  return "Patrulla";
    }
    return "Desconocido";
}

char ship_type_char(ShipType t) {
    switch (t) {
        case SHIP_NORMAL:  return 'N';
        case SHIP_FISHING: return 'F';
        case SHIP_PATROL:  return 'P';
    }
    return '?';
}

int ship_default_speed(ShipType t) {
    switch (t) {
        case SHIP_NORMAL:  return SPEED_NORMAL;
        case SHIP_FISHING: return SPEED_FISHING;
        case SHIP_PATROL:  return SPEED_PATROL;
    }
    return SPEED_NORMAL;
}

int ship_default_priority(ShipType t) {
    switch (t) {
        case SHIP_NORMAL:  return PRIORITY_NORMAL;
        case SHIP_FISHING: return PRIORITY_FISHING;
        case SHIP_PATROL:  return PRIORITY_PATROL;
    }
    return PRIORITY_NORMAL;
}

int ship_default_burst(ShipType t, int canal_length) {
    return ship_default_speed(t) * canal_length;
}

/* ── Función del hilo del barco ──────────────────────────────── */
static void *ship_thread_func(void *arg) {
    Ship  *s     = (Ship *)arg;
    Canal *canal = g_canal;

    /* Fase 1: encolar y esperar turno */
    s->state = STATE_WAITING;
    if (s->direction == DIR_LEFT)
        queue_push(&canal->queue_left, s);
    else
        queue_push(&canal->queue_right, s);

    canal_enter(canal, s);

    /* Fase 2: cruzar el canal */
    s->state      = STATE_CROSSING;
    s->start_time = now_ms();
    s->position   = 0;

    for (int i = 0; i < s->canal_length; i++) {
        pthread_mutex_lock(&canal->lock);
        s->position = i + 1;
        pthread_mutex_unlock(&canal->lock);
        usleep((unsigned int)(s->speed_ms * 1000));
    }

    /* Fase 3: salir */
    s->finish_time = now_ms();
    s->state       = STATE_DONE;
    canal_exit(canal, s);

    return NULL;
}

/* ── Crear barco ─────────────────────────────────────────────── */
Ship *ship_create(ShipType type, Direction dir,
                  int canal_length, int deadline_ms) {
    Ship *s = (Ship *)calloc(1, sizeof(Ship));
    if (!s) { perror("calloc ship"); return NULL; }

    pthread_mutex_lock(&g_id_mutex);
    s->id = g_ship_id_counter++;
    pthread_mutex_unlock(&g_id_mutex);

    s->type         = type;
    s->direction    = dir;
    s->canal_length = canal_length;
    s->speed_ms     = ship_default_speed(type);
    s->priority     = ship_default_priority(type);
    s->burst_ms     = ship_default_burst(type, canal_length);
    s->deadline_ms  = deadline_ms;
    s->state        = STATE_WAITING;
    s->position     = 0;
    s->arrival_time = now_ms();
    s->start_time   = 0;
    s->finish_time  = 0;
    s->next         = NULL;
    s->tid_created  = 0;

    int ret = pthread_create(&s->thread, NULL, ship_thread_func, s);
    if (ret != 0) {
        fprintf(stderr, "[ERROR] pthread_create barco %d: %d\n", s->id, ret);
        free(s);
        return NULL;
    }
    s->tid_created = 1;

    /* Patrulla: detach porque es tiempo real hard */
    if (type == SHIP_PATROL)
        pthread_detach(s->thread);

    return s;
}

/* ── Destruir barco ──────────────────────────────────────────── */
void ship_destroy(Ship *s) {
    if (!s) return;
    if (s->tid_created && s->type != SHIP_PATROL)
        pthread_join(s->thread, NULL);
    free(s);
}

/* ── Cola ────────────────────────────────────────────────────── */
void queue_init(ShipQueue *q) {
    q->head  = NULL;
    q->tail  = NULL;
    q->count = 0;
    pthread_mutex_init(&q->lock, NULL);
}

void queue_destroy(ShipQueue *q) {
    pthread_mutex_lock(&q->lock);
    q->head = q->tail = NULL;
    q->count = 0;
    pthread_mutex_unlock(&q->lock);
    pthread_mutex_destroy(&q->lock);
}

void queue_push(ShipQueue *q, Ship *s) {
    pthread_mutex_lock(&q->lock);
    s->next = NULL;
    if (!q->tail) {
        q->head = q->tail = s;
    } else {
        q->tail->next = s;
        q->tail       = s;
    }
    q->count++;
    pthread_mutex_unlock(&q->lock);
}

Ship *queue_pop(ShipQueue *q) {
    pthread_mutex_lock(&q->lock);
    if (!q->head) { pthread_mutex_unlock(&q->lock); return NULL; }
    Ship *s  = q->head;
    q->head  = s->next;
    if (!q->head) q->tail = NULL;
    s->next  = NULL;
    q->count--;
    pthread_mutex_unlock(&q->lock);
    return s;
}

Ship *queue_peek(ShipQueue *q) {
    pthread_mutex_lock(&q->lock);
    Ship *s = q->head;
    pthread_mutex_unlock(&q->lock);
    return s;
}