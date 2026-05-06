/*
 * ship.c
 * Ship creation and thread-based canal crossing simulation.
 *
 * CE 4303 – Scheduling Ships
 */

#include "ship.h"
#include "thread_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* usleep */

/* ─── Speed table (ms per canal unit) ────────────────────────── */
/* PATROL crosses fastest (fewer ms per unit) */
static const int SPEED_TABLE[] = {
    [SHIP_NORMAL]  = 80,   /* ms per unit */
    [SHIP_FISHING] = 50,
    [SHIP_PATROL]  = 25
};

/* ─── Default priorities ──────────────────────────────────────── */
int ship_default_priority(ShipType t) {
    switch (t) {
        case SHIP_NORMAL:  return 1;
        case SHIP_FISHING: return 5;
        case SHIP_PATROL:  return 10;
    }
    return 1;
}

/* ─── Default burst time (crossing time in ms) ────────────────── */
int ship_default_burst(ShipType t, int canal_length) {
    return SPEED_TABLE[t] * canal_length;
}

/* ─── Human-readable name ─────────────────────────────────────── */
const char *ship_type_name(ShipType t) {
    switch (t) {
        case SHIP_NORMAL:  return "Normal ";
        case SHIP_FISHING: return "Fishing";
        case SHIP_PATROL:  return "Patrol ";
    }
    return "Unknown";
}

/* ─── Ship thread function ────────────────────────────────────── */
void ship_run(void *arg) {
    Ship *s = (Ship *)arg;
    TCB  *self = thread_current();

    const char *dir_str = (s->direction == DIR_LEFT_TO_RIGHT) ? "L→R" : "R←L";

    printf("  [SHIP %3d | %-7s | %s] Entering canal  (tid=%d, pri=%d)\n",
           s->ship_id, ship_type_name(s->type), dir_str,
           self ? self->tid : -1,
           self ? self->priority : -1);
    fflush(stdout);

    /* Simulate crossing: advance position unit by unit */
    int speed_ms = SPEED_TABLE[s->type];
    for (s->position = 0; s->position < s->canal_length; s->position++) {
        /* Simulate one unit of movement */
        /* In the real project, the GUI reads s->position */
        usleep((unsigned int)(speed_ms * 1000));  /* ms → µs */

        /* Patrol ships don't yield (hard real-time) */
        if (s->type != SHIP_PATROL) {
            thread_yield();
        }
    }

    printf("  [SHIP %3d | %-7s | %s] Crossed canal   (tid=%d, time=%lld ms)\n",
           s->ship_id, ship_type_name(s->type), dir_str,
           self ? self->tid : -1,
           thread_time_ms());
    fflush(stdout);

    /* thread_exit() is called automatically by thread_wrapper/trampoline */
}

/* ─── Ship factory ────────────────────────────────────────────── */
static int g_ship_id_counter = 1;

Ship *ship_create(ShipType type, Direction direction,
                  int canal_length, int deadline_ms) {
    Ship *s = (Ship *)malloc(sizeof(Ship));
    if (!s) return NULL;

    memset(s, 0, sizeof(*s));
    s->ship_id     = g_ship_id_counter++;
    s->type        = type;
    s->direction   = direction;
    s->canal_length= canal_length;
    s->position    = 0;
    s->created_at  = thread_time_ms();

    int priority = ship_default_priority(type);
    int burst    = ship_default_burst(type, canal_length);

    /* If EDF is active and no explicit deadline, use burst*2 as deadline */
    if (deadline_ms == 0 && type == SHIP_PATROL)
        deadline_ms = burst;        /* Hard RT: deadline == burst */
    else if (deadline_ms == 0)
        deadline_ms = burst * 3;    /* Soft: generous deadline    */

    /* Create the underlying thread */
    char name[32];
    snprintf(name, sizeof(name), "Ship%03d-%s",
             s->ship_id, ship_type_name(type));

    s->tid = thread_create(ship_run, s, name,
                           priority, burst, deadline_ms);
    return s;
}