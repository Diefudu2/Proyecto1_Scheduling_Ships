#ifndef SHIP_H
#define SHIP_H

#include "thread_lib.h"

/* ─── Ship types ─────────────────────────────────────────────── */
typedef enum {
    SHIP_NORMAL,    /* Slowest, no special treatment               */
    SHIP_FISHING,   /* Faster than normal                          */
    SHIP_PATROL     /* Fastest, hard real-time (EDF / urgent)      */
} ShipType;

/* ─── Direction ──────────────────────────────────────────────── */
typedef enum {
    DIR_LEFT_TO_RIGHT = 0,
    DIR_RIGHT_TO_LEFT = 1
} Direction;

/* ─── Speed constants (arbitrary units, used for animation/sim) ─ */
#define SPEED_NORMAL   1
#define SPEED_FISHING  2
#define SPEED_PATROL   4

/* ─── Ship data (passed as arg to the thread function) ──────── */
typedef struct {
    int       ship_id;
    ShipType  type;
    Direction direction;
    int       canal_length;   /* Cells / pixels to traverse       */
    int       position;       /* Current position in canal (0..N) */
    int       tid;            /* Thread ID assigned by lib        */
    long long created_at;     /* Timestamp of creation (ms)       */
} Ship;

/* ─── Ship factory ───────────────────────────────────────────── */

/**
 * Create a ship thread.
 * The ship's type determines default priority, speed and deadline.
 *
 * @param type          SHIP_NORMAL / SHIP_FISHING / SHIP_PATROL
 * @param direction     Which side of the canal it originates from
 * @param canal_length  Length of the canal in simulation units
 * @param deadline_ms   Max time to cross (0 = not real-time)
 * @return              Pointer to allocated Ship struct, or NULL on error
 */
Ship *ship_create(ShipType type, Direction direction,
                  int canal_length, int deadline_ms);

/**
 * The thread entry point – simulates crossing the canal.
 * Passed as `func` to thread_create.
 */
void ship_run(void *arg);

/**
 * Return a human-readable name for a ship type.
 */
const char *ship_type_name(ShipType t);

/**
 * Return the base priority of a ship type.
 * PATROL > FISHING > NORMAL
 */
int ship_default_priority(ShipType t);

/**
 * Return the simulated burst time (cross time) in ms for a ship type.
 */
int ship_default_burst(ShipType t, int canal_length);

#endif /* SHIP_H */