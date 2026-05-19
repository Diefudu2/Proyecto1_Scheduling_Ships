/* ============================================================
 * Archivo: ships.c
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Gestiona creación, atributos por tipo, estados y sincronización de barcos con SimThread.
 *
 * Documentación interna:
 * - Mantener este módulo pequeño, con validaciones defensivas y sin asumir entradas válidas.
 *
 * Convenciones:
 * - Las funciones públicas se declaran en el .h correspondiente.
 * - Las funciones static son utilidades internas del archivo.
 * - Retornos int usan 1=éxito/verdadero y 0=fallo/falso salvo que se indique otra cosa.
 * ============================================================ */
#include "ships.h"
#include "scheduler.h"

#include <string.h>

static Ship g_ships[MAX_SHIPS];
static int g_ship_count = 0;
static int g_next_ship_id = 1;

// Inicializa el estado global de los barcos y prepara la lista para nuevas creaciones.
void ships_init(void)
{
    memset(g_ships, 0, sizeof(g_ships));
    g_ship_count = 0;
    g_next_ship_id = 1;
}

// Crea un nuevo barco con tipo y dirección, asigna hilo y lo pone en cola ready.
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

    // Marca el barco como en ejecución y sincroniza el tiempo restante.
    s->state = SHIP_RUNNING;
    s->remaining_ms = s->thread->remaining_ms;
}

// Devuelve nombre amigable del tipo de barco.
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

// Devuelve el nombre legible del estado del barco.
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

// Cuenta todos los barcos en la dirección indicada que todavía no han terminado.
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

// Cuenta los barcos que están listos para ejecutarse en la dirección indicada.
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

// Devuelve la prioridad por defecto según el tipo de barco.
int ship_default_priority(ShipType type)
{
    switch (type) {
        case SHIP_NORMAL: return 1;
        case SHIP_FISHER: return 5;
        case SHIP_PATROL: return 10;
        default:          return 1;
    }
}

// Devuelve el tiempo de ráfaga por defecto según el tipo de barco.
int ship_default_burst_ms(ShipType type)
{
    switch (type) {
        case SHIP_NORMAL: return 6000;
        case SHIP_FISHER: return 4000;
        case SHIP_PATROL: return 2000;
        default:          return 6000;
    }
}

// Devuelve el plazo por defecto según el tipo de barco.
int ship_default_deadline_ms(ShipType type)
{
    switch (type) {
        case SHIP_NORMAL: return 15000;
        case SHIP_FISHER: return 10000;
        case SHIP_PATROL: return 5000;
        default:          return 15000;
    }
}

// Acceso a la tabla interna de barcos para lectura/sincronización.
Ship *ships_get_all(void)
{
    return g_ships;
}

int ships_get_count(void)
{
    return g_ship_count;
}

// Parsea una cadena de texto y devuelve el tipo de barco correspondiente.
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

// Traduce la dirección de texto a un valor interno de dirección de barco.
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

// Sincroniza los estados internos de los barcos con el estado actual de sus hilos.
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
