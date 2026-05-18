#include "ships.h"
#include "scheduler.h"

#include <string.h>

static Ship g_ships[MAX_SHIPS];
static int g_ship_count = 0;
static int g_next_ship_id = 1;

void ships_init(void)
{
    memset(g_ships, 0, sizeof(g_ships));
    g_ship_count = 0;
    g_next_ship_id = 1;
}

Ship *ship_create(ShipType type, ShipDir dir)
{
    if (g_ship_count >= CONFIG_MAX_SHIPS) {
        return NULL;
    }

    SystemConfig *cfg = config_get();

    if (ships_count_ready_by_dir(dir) >= cfg->max_queue_per_side) {
        return NULL;
    }

    Ship *s = &g_ships[g_ship_count];
    memset(s, 0, sizeof(*s));

    s->id = g_next_ship_id++;
    s->type = type;
    s->dir = dir;
    s->state = SHIP_READY;
    s->position = -1;
    s->saved_position = -1;
    s->speed = ship_default_speed(type);
    s->priority = ship_default_priority(type);
    s->burst_ms = ship_default_burst_ms(type);
    s->remaining_ms = s->burst_ms;
    s->deadline_ms = ship_default_deadline_ms(type);

    s->thread = thread_create(
        ship_thread_step,
        s,
        s->priority,
        s->burst_ms,
        s->deadline_ms
    );

    if (!s->thread) {
        memset(s, 0, sizeof(*s));
        return NULL;
    }

    scheduler_add_ready(s->thread);
    g_ship_count++;
    return s;
}

void ship_thread_step(void *arg)
{
    Ship *s = (Ship *)arg;

    if (!s || !s->thread) {
        return;
    }

    s->state = SHIP_RUNNING;
    s->remaining_ms = s->thread->remaining_ms;
}

const char *ship_type_name(ShipType type)
{
    switch (type) {
        case SHIP_NORMAL: return "NORMAL";
        case SHIP_FISHER: return "FISHER";
        case SHIP_PATROL: return "PATROL";
        default:          return "UNKNOWN";
    }
}

const char *ship_dir_name(ShipDir dir)
{
    switch (dir) {
        case DIR_LEFT_TO_RIGHT: return "L";
        case DIR_RIGHT_TO_LEFT: return "R";
        default:                return "?";
    }
}

const char *ship_state_name(ShipState state)
{
    switch (state) {
        case SHIP_WAITING:  return "WAITING";
        case SHIP_READY:    return "READY";
        case SHIP_RUNNING:  return "RUNNING";
        case SHIP_BLOCKED:  return "BLOCKED";
        case SHIP_CROSSING: return "CROSSING";
        case SHIP_PAUSED:   return "PAUSED";
        case SHIP_DONE:     return "DONE";
        default:            return "UNKNOWN";
    }
}

int ships_count_by_dir(ShipDir dir)
{
    int count = 0;

    for (int i = 0; i < g_ship_count; i++) {
        if (g_ships[i].dir == dir && g_ships[i].state != SHIP_DONE) {
            count++;
        }
    }

    return count;
}

int ships_count_ready_by_dir(ShipDir dir)
{
    int count = 0;

    for (int i = 0; i < g_ship_count; i++) {
        Ship *s = &g_ships[i];

        if (!s->thread) {
            continue;
        }

        if (s->dir == dir && s->thread->state == THREAD_READY) {
            count++;
        }
    }

    return count;
}

int ship_default_speed(ShipType type)
{
    switch (type) {
        case SHIP_NORMAL: return 1;
        case SHIP_FISHER: return 2;
        case SHIP_PATROL: return 3;
        default:          return 1;
    }
}

int ship_default_priority(ShipType type)
{
    switch (type) {
        case SHIP_NORMAL: return 1;
        case SHIP_FISHER: return 5;
        case SHIP_PATROL: return 10;
        default:          return 1;
    }
}

int ship_default_burst_ms(ShipType type)
{
    switch (type) {
        case SHIP_NORMAL: return 6000;
        case SHIP_FISHER: return 4000;
        case SHIP_PATROL: return 2000;
        default:          return 6000;
    }
}

int ship_default_deadline_ms(ShipType type)
{
    switch (type) {
        case SHIP_NORMAL: return 15000;
        case SHIP_FISHER: return 10000;
        case SHIP_PATROL: return 5000;
        default:          return 15000;
    }
}

Ship *ships_get_all(void)
{
    return g_ships;
}

int ships_get_count(void)
{
    return g_ship_count;
}

int ship_parse_type(const char *text, ShipType *out)
{
    if (!text || !out) {
        return 0;
    }

    if (strcmp(text, "NORMAL") == 0 || strcmp(text, "N") == 0) {
        *out = SHIP_NORMAL;
        return 1;
    }

    if (strcmp(text, "FISHER") == 0 || strcmp(text, "F") == 0 ||
        strcmp(text, "PESQUERO") == 0) {
        *out = SHIP_FISHER;
        return 1;
    }

    if (strcmp(text, "PATROL") == 0 || strcmp(text, "P") == 0 ||
        strcmp(text, "PATRULLA") == 0) {
        *out = SHIP_PATROL;
        return 1;
    }

    return 0;
}

int ship_parse_dir(const char *text, ShipDir *out)
{
    if (!text || !out) {
        return 0;
    }

    if (strcmp(text, "L") == 0 || strcmp(text, "LEFT") == 0) {
        *out = DIR_LEFT_TO_RIGHT;
        return 1;
    }

    if (strcmp(text, "R") == 0 || strcmp(text, "RIGHT") == 0) {
        *out = DIR_RIGHT_TO_LEFT;
        return 1;
    }

    return 0;
}

void ships_sync_states_from_threads(void)
{
    for (int i = 0; i < g_ship_count; i++) {
        Ship *s = &g_ships[i];

        if (!s->thread) {
            continue;
        }

        s->remaining_ms = s->thread->remaining_ms;

        switch (s->thread->state) {
            case THREAD_READY:
                s->state = SHIP_READY;
                break;

            case THREAD_RUNNING:
                if (s->position >= 0) {
                    s->state = SHIP_CROSSING;
                } else {
                    s->state = SHIP_RUNNING;
                }
                break;

            case THREAD_BLOCKED:
                s->state = SHIP_BLOCKED;
                break;

            case THREAD_PREEMPTED:
                s->state = SHIP_READY;
                break;

            case THREAD_PAUSED:
                s->state = SHIP_PAUSED;
                break;

            case THREAD_DONE:
                s->state = SHIP_DONE;
                break;

            default:
                break;
        }
    }
}
