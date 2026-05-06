#ifndef SHIP_H
#define SHIP_H

#include <pthread.h>

/* ── Tipos de barco ─────────────────────────────────────────────── */
typedef enum {
    SHIP_NORMAL  = 0,
    SHIP_FISHING = 1,
    SHIP_PATROL  = 2
} ShipType;

/* ── Dirección ──────────────────────────────────────────────────── */
typedef enum {
    DIR_LEFT  = 0,
    DIR_RIGHT = 1
} Direction;

/* ── Estado del barco ───────────────────────────────────────────── */
typedef enum {
    STATE_WAITING  = 0,
    STATE_CROSSING = 1,
    STATE_DONE     = 2
} ShipState;

/* ── Velocidades (ms por unidad de canal) ────────────────────────*/
#define SPEED_NORMAL   120
#define SPEED_FISHING   70
#define SPEED_PATROL    35

/* ── Prioridades ─────────────────────────────────────────────────*/
#define PRIORITY_NORMAL   1
#define PRIORITY_FISHING  5
#define PRIORITY_PATROL  10

/* ── Estructura principal del barco ──────────────────────────────*/
typedef struct Ship {
    int        id;
    ShipType   type;
    Direction  direction;
    ShipState  state;

    int        position;
    int        canal_length;
    int        speed_ms;
    int        priority;
    int        burst_ms;
    int        deadline_ms;

    long long  arrival_time;
    long long  start_time;
    long long  finish_time;

    pthread_t  thread;
    int        tid_created;

    struct Ship *next;
} Ship;

/* ── Cola de barcos ──────────────────────────────────────────────*/
typedef struct {
    Ship           *head;
    Ship           *tail;
    int             count;
    pthread_mutex_t lock;
} ShipQueue;

/* ── API ─────────────────────────────────────────────────────────*/
Ship       *ship_create          (ShipType type, Direction dir,
                                  int canal_length, int deadline_ms);
void        ship_destroy         (Ship *s);

void        queue_init           (ShipQueue *q);
void        queue_destroy        (ShipQueue *q);
void        queue_push           (ShipQueue *q, Ship *s);
Ship       *queue_pop            (ShipQueue *q);
Ship       *queue_peek           (ShipQueue *q);

const char *ship_type_name       (ShipType t);
char        ship_type_char       (ShipType t);
int         ship_default_speed   (ShipType t);
int         ship_default_priority(ShipType t);
int         ship_default_burst   (ShipType t, int canal_length);
long long   now_ms               (void);

#endif /* SHIP_H */