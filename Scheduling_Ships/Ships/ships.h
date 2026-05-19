#ifndef SHIPS_H
#define SHIPS_H

/* ============================================================
 * Archivo: ships.h
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Define tipos, estados y estructura Ship.
 *
 * Este encabezado contiene la API pública del módulo. Mantener aquí solo
 * tipos, constantes y prototipos requeridos por otros archivos.
 * ============================================================ */
#include "config.h"

#include "thread.h"

#define MAX_SHIPS CONFIG_MAX_SHIPS

typedef enum {
    SHIP_NORMAL = 0,
    SHIP_FISHER,
    SHIP_PATROL
} ShipType;

typedef enum {
    DIR_LEFT_TO_RIGHT = 0,
    DIR_RIGHT_TO_LEFT
} ShipDir;

typedef enum {
    SHIP_WAITING = 0,
    SHIP_READY,
    SHIP_RUNNING,
    SHIP_BLOCKED,
    SHIP_CROSSING,
    SHIP_PAUSED,
    SHIP_DONE
} ShipState;

typedef struct Ship {
    int id;

    ShipType type;
    ShipDir dir;
    ShipState state;

    int position;
    int saved_position;
    int speed;

    int priority;
    int burst_ms;
    int remaining_ms;
    int deadline_ms;

    SimThread *thread;

    struct Ship *next;
} Ship;

void ships_init(void);

Ship *ship_create(ShipType type, ShipDir dir);
void ship_thread_step(void *arg);

const char *ship_type_name(ShipType type);
const char *ship_dir_name(ShipDir dir);
const char *ship_state_name(ShipState state);

int ship_default_speed(ShipType type);
int ship_default_priority(ShipType type);
int ship_default_burst_ms(ShipType type);
int ship_default_deadline_ms(ShipType type);
int ships_count_by_dir(ShipDir dir);
int ships_count_ready_by_dir(ShipDir dir);
void ships_sync_states_from_threads(void);

Ship *ships_get_all(void);
int ships_get_count(void);

int ship_parse_type(const char *text, ShipType *out);
int ship_parse_dir(const char *text, ShipDir *out);

#endif