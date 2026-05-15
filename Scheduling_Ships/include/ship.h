#ifndef SHIP_H
#define SHIP_H

#include "uthread.h"

#include <time.h>

/* =========================================================
 * ship.h — Modelo de barco como hilo de usuario
 *
 * Versión compatible con el canal.h nuevo.
 *
 * Cada barco:
 *   - tiene estado propio,
 *   - pertenece a una dirección,
 *   - tiene parámetros de scheduling,
 *   - se ejecuta como un UThread.
 * ========================================================= */


/* =========================================================
 * Tipos de barco
 * ========================================================= */

typedef enum {
    SHIP_NORMAL = 0,
    SHIP_FISHER = 1,
    SHIP_PATROL = 2
} ShipType;


/* =========================================================
 * Dirección del barco
 *
 * DIR_LEFT:
 *   Sale del lado izquierdo y cruza hacia la derecha.
 *
 * DIR_RIGHT:
 *   Sale del lado derecho y cruza hacia la izquierda.
 * ========================================================= */

typedef enum {
    DIR_LEFT = 0,
    DIR_RIGHT = 1
} ShipDir;


/* =========================================================
 * Estado lógico del barco
 * ========================================================= */

typedef enum {
    SHIP_WAITING = 0,
    SHIP_BLOCKED = 1,
    SHIP_CROSSING = 2,
    SHIP_DONE = 3
} ShipState;


/* =========================================================
 * Estructura principal del barco
 * ========================================================= */

typedef struct Ship {
    int id;

    ShipType  type;
    ShipDir   dir;
    ShipState state;

    /*
     * Posición y movimiento.
     *
     * pos:
     *   posición actual dentro del canal.
     *
     * canal_len:
     *   largo total del canal.
     *
     * speed:
     *   unidades avanzadas por tick.
     */
    int pos;
    int canal_len;
    int speed;

    /*
     * Parámetros de scheduling.
     */
    int priority;
    int burst_ms;
    int deadline_ms;
    long arrival_seq;

    /*
     * Métricas de tiempo.
     */
    struct timespec arrival_time;
    struct timespec start_time;
    struct timespec finish_time;

    /*
     * UThread asociado al barco.
     */
    UThread *uth;

    /*
     * Enlace para ShipQueue.
     */
    struct Ship *next;
} Ship;


/* =========================================================
 * Cola de barcos
 *
 * En el canal.h nuevo, el mutex está en Canal:
 *
 *     c->mutex
 *
 * Por eso ShipQueue no tiene mutex propio.
 * ========================================================= */

typedef struct {
    Ship *head;
    Ship *tail;
    int   count;
} ShipQueue;


/* =========================================================
 * API de barcos
 * ========================================================= */

Ship *ship_create(ShipType type,
                  ShipDir dir,
                  int priority,
                  int burst_ms,
                  int deadline_ms);

void ship_run(void *arg);

void ship_destroy(Ship *s);


/* =========================================================
 * API de cola de barcos
 * ========================================================= */

void  queue_push(ShipQueue *q, Ship *s);
Ship *queue_pop(ShipQueue *q);
void  queue_remove(ShipQueue *q, Ship *s);


/* =========================================================
 * Utilidades de barco
 * ========================================================= */

const char *ship_type_name(ShipType t);
char        ship_type_char(ShipType t);

int ship_default_speed(ShipType t);
int ship_default_priority(ShipType t);
int ship_default_burst(ShipType t, int canal_length);

#endif /* SHIP_H */