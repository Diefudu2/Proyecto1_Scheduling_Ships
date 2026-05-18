#include "canal.h"
#include "scheduler.h"

#include <stdio.h>
#include <string.h>

/* =========================================================
 * canal.c
 *
 * Canal lógico del sistema.
 * - El largo lógico puede ser mayor que los 10 LEDs físicos.
 * - La colisión se protege por positions[] y semáforos.
 * - Los barcos avanzan por posiciones lógicas según speed.
 * - No se permite rebase.
 * ========================================================= */

static Canal g_canal;

/* =========================================================
 * Prototipos internos
 * ========================================================= */

static int canal_valid_position(int position);
static int canal_ship_has_finished(Ship *ship);
static void canal_finish_ship(Ship *ship);
static int canal_advance_one_position(Ship *ship);
static void canal_move_ship(Ship *ship);

static CanalDirection ship_dir_to_canal_dir(ShipDir dir);
static ShipDir canal_dir_to_ship_dir(CanalDirection dir);
static CanalDirection canal_opposite_dir(CanalDirection dir);

static int canal_entry_position_for_ship(Ship *ship);
static int canal_next_position_for_ship(Ship *ship);

static int canal_visual_slot_has_other_ship(int slot, Ship *self);
static int canal_has_waiting_in_dir(CanalDirection dir);
static int canal_flow_allows_entry(Ship *ship);

/* Estado de flujo */
static CanalDirection g_equidad_turn_dir = CANAL_DIR_LEFT_TO_RIGHT;
static int g_equidad_passed_in_turn = 0;

static CanalDirection g_letrero_dir = CANAL_DIR_LEFT_TO_RIGHT;
static int g_letrero_elapsed_ms = 0;
static int g_letrero_open = 1;
static int g_letrero_counting = 0;

static CanalDirection g_tico_turn_dir = CANAL_DIR_LEFT_TO_RIGHT;
static int g_tico_batch_closed = 0;

static int canal_valid_position(int position)
{
    return position >= 0 && position < g_canal.length;
}

static CanalDirection ship_dir_to_canal_dir(ShipDir dir)
{
    return (dir == DIR_LEFT_TO_RIGHT)
           ? CANAL_DIR_LEFT_TO_RIGHT
           : CANAL_DIR_RIGHT_TO_LEFT;
}

static ShipDir canal_dir_to_ship_dir(CanalDirection dir)
{
    return (dir == CANAL_DIR_LEFT_TO_RIGHT)
           ? DIR_LEFT_TO_RIGHT
           : DIR_RIGHT_TO_LEFT;
}

static CanalDirection canal_opposite_dir(CanalDirection dir)
{
    if (dir == CANAL_DIR_LEFT_TO_RIGHT) {
        return CANAL_DIR_RIGHT_TO_LEFT;
    }

    if (dir == CANAL_DIR_RIGHT_TO_LEFT) {
        return CANAL_DIR_LEFT_TO_RIGHT;
    }

    return CANAL_DIR_FREE;
}

static int canal_entry_position_for_ship(Ship *ship)
{
    if (!ship || g_canal.length <= 0) {
        return -1;
    }

    return (ship->dir == DIR_LEFT_TO_RIGHT) ? 0 : (g_canal.length - 1);
}

static int canal_next_position_for_ship(Ship *ship)
{
    if (!ship) {
        return -1;
    }

    return (ship->dir == DIR_LEFT_TO_RIGHT)
           ? (ship->position + 1)
           : (ship->position - 1);
}

static int canal_ship_has_finished(Ship *ship)
{
    if (!ship) {
        return 0;
    }

    if (!canal_valid_position(ship->position)) {
        return 0;
    }

    if (ship->dir == DIR_LEFT_TO_RIGHT) {
        return ship->position >= (g_canal.length - 1);
    }

    if (ship->dir == DIR_RIGHT_TO_LEFT) {
        return ship->position <= 0;
    }

    return 0;
}

int canal_position_to_led_slot(int position)
{
    if (g_canal.length <= 0) {
        return -1;
    }

    if (position < 0 || position >= g_canal.length) {
        return -1;
    }

    /*
     * Mapea posición lógica a slot físico 0..LED_CANAL_COUNT-1.
     *
     * Ejemplo:
     * canal_length = 20
     * LED_CANAL_COUNT = 10
     *
     * pos 0-1   -> slot 0
     * pos 2-3   -> slot 1
     * ...
     * pos 18-19 -> slot 9
     */
    int slot = (position * LED_CANAL_COUNT) / g_canal.length;

    if (slot < 0) {
        slot = 0;
    }

    if (slot >= LED_CANAL_COUNT) {
        slot = LED_CANAL_COUNT - 1;
    }

    return slot;
}

int canal_position_to_led_index(int position)
{
    int slot = canal_position_to_led_slot(position);

    if (slot < 0) {
        return -1;
    }

    return LED_CANAL_START + slot;
}

static int canal_visual_slot_has_other_ship(int slot, Ship *self)
{
    if (slot < 0 || slot >= LED_CANAL_COUNT) {
        return 1;
    }

    for (int pos = 0; pos < g_canal.length; pos++) {
        Ship *other = g_canal.positions[pos];

        if (!other || other == self) {
            continue;
        }

        if (canal_position_to_led_slot(pos) == slot) {
            return 1;
        }
    }

    return 0;
}


static int canal_has_waiting_in_dir(CanalDirection dir)
{
    if (dir == CANAL_DIR_FREE) {
        return 0;
    }

    return scheduler_has_ready_ship_dir(canal_dir_to_ship_dir(dir));
}

static void canal_reset_flow_state(void)
{
    g_equidad_turn_dir = CANAL_DIR_LEFT_TO_RIGHT;
    g_equidad_passed_in_turn = 0;

    g_letrero_dir = CANAL_DIR_LEFT_TO_RIGHT;
    g_letrero_elapsed_ms = 0;
    g_letrero_open = 1;
    g_letrero_counting = 0;

    g_tico_turn_dir = CANAL_DIR_LEFT_TO_RIGHT;
    g_tico_batch_closed = 0;
}

static void canal_update_flow_when_empty(void)
{
    SystemConfig *cfg = config_get();

    if (g_canal.ship_count != 0) {
        return;
    }

    g_canal.active_dir = CANAL_DIR_FREE;

    if (cfg->flow_algo == FLOW_LETRERO) {
        if (!g_letrero_open) {
            g_letrero_dir = canal_opposite_dir(g_letrero_dir);
            g_letrero_elapsed_ms = 0;
            g_letrero_counting = 0;
            g_letrero_open = 1;
            return;
        }

        /* Si el lado del letrero no tiene barcos, pero el opuesto sí, se cambia para no bloquear. */
        CanalDirection opposite = canal_opposite_dir(g_letrero_dir);
        if (!canal_has_waiting_in_dir(g_letrero_dir) && canal_has_waiting_in_dir(opposite)) {
            g_letrero_dir = opposite;
            g_letrero_elapsed_ms = 0;
            g_letrero_counting = 0;
            g_letrero_open = 1;
        }
    }
    else if (cfg->flow_algo == FLOW_EQUIDAD) {
        CanalDirection opposite = canal_opposite_dir(g_equidad_turn_dir);

        if (cfg->equidad_w <= 0) {
            return;
        }

        if (g_equidad_passed_in_turn >= cfg->equidad_w && canal_has_waiting_in_dir(opposite)) {
            g_equidad_turn_dir = opposite;
            g_equidad_passed_in_turn = 0;
            return;
        }

        if (!canal_has_waiting_in_dir(g_equidad_turn_dir) && canal_has_waiting_in_dir(opposite)) {
            g_equidad_turn_dir = opposite;
            g_equidad_passed_in_turn = 0;
        }
    }
    else { /* FLOW_TICO */
        CanalDirection opposite = canal_opposite_dir(g_tico_turn_dir);

        if (g_tico_batch_closed) {
            g_tico_turn_dir = opposite;
            g_tico_batch_closed = 0;
            return;
        }

        if (!canal_has_waiting_in_dir(g_tico_turn_dir) && canal_has_waiting_in_dir(opposite)) {
            g_tico_turn_dir = opposite;
        }
    }
}

static int canal_flow_allows_entry(Ship *ship)
{
    if (!ship) {
        return 0;
    }

    SystemConfig *cfg = config_get();
    CanalDirection desired_dir = ship_dir_to_canal_dir(ship->dir);
    CanalDirection opposite;

    switch (cfg->flow_algo) {
        case FLOW_EQUIDAD:
            if (cfg->equidad_w <= 0) {
                return 1;
            }

            opposite = canal_opposite_dir(g_equidad_turn_dir);

            if (!canal_has_waiting_in_dir(g_equidad_turn_dir) && canal_has_waiting_in_dir(opposite)) {
                g_equidad_turn_dir = opposite;
                g_equidad_passed_in_turn = 0;
            }

            opposite = canal_opposite_dir(g_equidad_turn_dir);

            if (g_equidad_passed_in_turn >= cfg->equidad_w && canal_has_waiting_in_dir(opposite)) {
                return 0;
            }

            return desired_dir == g_equidad_turn_dir;

        case FLOW_LETRERO:
            return g_letrero_open && desired_dir == g_letrero_dir;

        case FLOW_TICO:
        default: {
            int waiting_left = canal_has_waiting_in_dir(CANAL_DIR_LEFT_TO_RIGHT);
            int waiting_right = canal_has_waiting_in_dir(CANAL_DIR_RIGHT_TO_LEFT);

            if (waiting_left && waiting_right) {
                if (g_canal.ship_count > 0 && g_canal.active_dir == desired_dir) {
                    /* Cuando hay competencia de ambos lados, TICO cierra el lote actual
                     * después de permitir que el canal se llene inicialmente. */
                    return !g_tico_batch_closed;
                }

                return desired_dir == g_tico_turn_dir;
            }

            return 1;
        }
    }
}

void canal_init(void)
{
    memset(&g_canal, 0, sizeof(g_canal));
    canal_apply_config();
    canal_reset_flow_state();
}

int canal_apply_config(void)
{
    SystemConfig *cfg = config_get();

    if (g_canal.ship_count > 0) {
        return 0;
    }

    int new_length = cfg->canal_length;
    int new_max = cfg->max_ships_in_canal;

    if (new_length <= 0) {
        new_length = 1;
    }

    if (new_length > CONFIG_MAX_CANAL_POSITIONS) {
        new_length = CONFIG_MAX_CANAL_POSITIONS;
    }

    if (new_max <= 0) {
        new_max = 1;
    }

    if (new_max > new_length) {
        new_max = new_length;
    }

    g_canal.length = new_length;
    g_canal.max_ships = new_max;
    g_canal.active_dir = CANAL_DIR_FREE;
    g_canal.ship_count = 0;
    g_canal.interrupted = 0;

    for (int i = 0; i < CONFIG_MAX_CANAL_POSITIONS; i++) {
        g_canal.positions[i] = NULL;
        sim_sem_init(&g_canal.sem_positions[i], 1);
    }

    sim_sem_init(&g_canal.sem_cpu_slots, g_canal.max_ships);
    canal_reset_flow_state();

    return 1;
}

int canal_try_enter(Ship *ship)
{
    if (!ship || !ship->thread) {
        return 0;
    }

    if (g_canal.interrupted) {
        return 0;
    }

    if (g_canal.length <= 0 || g_canal.length > CONFIG_MAX_CANAL_POSITIONS) {
        return 0;
    }

    if (g_canal.max_ships <= 0 || g_canal.max_ships > g_canal.length) {
        return 0;
    }

    if (g_canal.ship_count >= g_canal.max_ships) {
        return 0;
    }

    CanalDirection desired_dir = ship_dir_to_canal_dir(ship->dir);

    /*
     * El canal solo puede tener un sentido activo.
     */
    if (g_canal.active_dir != CANAL_DIR_FREE &&
        g_canal.active_dir != desired_dir) {
        return 0;
    }

    /*
     * TICO / EQUIDAD / LETRERO deciden si este lado puede entrar.
     */
    if (!canal_flow_allows_entry(ship)) {
        return 0;
    }

    int entry_pos;

    /*
    * Si el barco fue apropiado antes, intenta volver a su posición guardada.
    * Si esa posición ya no está disponible, simplemente no entra todavía.
    */
    if (canal_valid_position(ship->saved_position)) {
        entry_pos = ship->saved_position;
    } else {
        entry_pos = canal_entry_position_for_ship(ship);
    }

    if (!canal_valid_position(entry_pos)) {
        return 0;
    }

    /*
     * Protección lógica: no puede entrar si la posición lógica
     * de entrada está ocupada.
     */
    if (g_canal.positions[entry_pos] != NULL) {
        return 0;
    }

    /*
     * Protección visual/física:
     * si el canal lógico es mayor que los LEDs físicos, varias
     * posiciones lógicas caen sobre el mismo LED físico.
     *
     * Por eso no se permite entrar si el segmento visual de entrada
     * ya está ocupado por otro barco.
     */
    int entry_slot = canal_position_to_led_slot(entry_pos);

    if (entry_slot < 0 || entry_slot >= LED_CANAL_COUNT) {
        return 0;
    }

    if (canal_visual_slot_has_other_ship(entry_slot, ship)) {
        return 0;
    }

    /*
     * Semáforo de cupo general del canal.
     */
    if (!sim_sem_wait(&g_canal.sem_cpu_slots, ship->thread)) {
        return 0;
    }

    /*
     * Semáforo de posición lógica de entrada.
     * Si falla, se libera el cupo general.
     */
    if (!sim_sem_wait(&g_canal.sem_positions[entry_pos], ship->thread)) {
        sim_sem_signal(&g_canal.sem_cpu_slots);
        return 0;
    }

    g_canal.positions[entry_pos] = ship;
    g_canal.ship_count++;
    g_canal.active_dir = desired_dir;

    ship->position = entry_pos;
    ship->state = SHIP_CROSSING;
    ship->saved_position = -1;

    thread_set_running(ship->thread);

    SystemConfig *cfg = config_get();

    /*
     * Actualización de estado de flujo.
     */
    if (cfg->flow_algo == FLOW_EQUIDAD &&
        desired_dir == g_equidad_turn_dir) {
        g_equidad_passed_in_turn++;
    }

    if (cfg->flow_algo == FLOW_LETRERO &&
        desired_dir == g_letrero_dir) {
        g_letrero_counting = 1;
    }

    /*
     * En TICO, si hay barcos esperando en ambos lados,
     * se cierra el lote actual para evitar que un lado drene
     * indefinidamente por prioridad.
     */
    if (cfg->flow_algo == FLOW_TICO) {
        int waiting_left = canal_has_waiting_in_dir(CANAL_DIR_LEFT_TO_RIGHT);
        int waiting_right = canal_has_waiting_in_dir(CANAL_DIR_RIGHT_TO_LEFT);

        if (waiting_left && waiting_right) {
            g_tico_batch_closed = 1;
        }
    }

    return 1;
}

static void canal_finish_ship(Ship *ship)
{
    if (!ship || !ship->thread) {
        return;
    }

    int pos = ship->position;

    if (canal_valid_position(pos) && g_canal.positions[pos] == ship) {
        g_canal.positions[pos] = NULL;
        sim_sem_signal(&g_canal.sem_positions[pos]);
    }

    sim_sem_signal(&g_canal.sem_cpu_slots);

    if (g_canal.ship_count > 0) {
        g_canal.ship_count--;
    }

    ship->position = -1;
    ship->state = SHIP_DONE;
    thread_exit(ship->thread);

    canal_update_flow_when_empty();
}

int canal_preempt_ship(Ship *ship)
{
    if (!ship || !ship->thread) {
        return 0;
    }

    if (ship->state != SHIP_CROSSING) {
        return 0;
    }

    int pos = ship->position;

    if (!canal_valid_position(pos)) {
        return 0;
    }

    if (g_canal.positions[pos] != ship) {
        return 0;
    }

    /*
     * Guardar punto lógico donde iba el barco.
     * Luego, cuando vuelva a ser calendarizado, intentará restaurarse ahí.
     */
    ship->saved_position = pos;

    /*
     * Liberar recurso lógico de posición.
     */
    g_canal.positions[pos] = NULL;
    sim_sem_signal(&g_canal.sem_positions[pos]);

    /*
     * Liberar cupo general del canal.
     */
    sim_sem_signal(&g_canal.sem_cpu_slots);

    if (g_canal.ship_count > 0) {
        g_canal.ship_count--;
    }

    /*
     * El barco vuelve a READY sin perder su remaining_ms.
     */
    ship->position = -1;
    ship->state = SHIP_READY;
    thread_preempt(ship->thread);

    scheduler_add_ready(ship->thread);

    /*
     * Si el canal quedó vacío, se actualiza el flujo.
     */
    canal_update_flow_when_empty();

    return 1;
}

int canal_has_crossing_ships(void)
{
    return g_canal.ship_count > 0;
}

static int canal_advance_one_position(Ship *ship)
{
    if (!ship || !ship->thread) {
        return 0;
    }

    if (ship->state != SHIP_CROSSING) {
        return 0;
    }

    int current_pos = ship->position;

    if (!canal_valid_position(current_pos)) {
        return 0;
    }

    /*
     * Si ya llegó al extremo de salida, finaliza.
     */
    if (canal_ship_has_finished(ship)) {
        canal_finish_ship(ship);
        return 0;
    }

    int next_pos = canal_next_position_for_ship(ship);

    /*
     * Si el siguiente paso se sale del canal, finaliza.
     */
    if (!canal_valid_position(next_pos)) {
        canal_finish_ship(ship);
        return 0;
    }

    /*
     * Protección lógica:
     * si la siguiente posición lógica está ocupada,
     * el barco se queda esperando. No rebasa.
     */
    if (g_canal.positions[next_pos] != NULL) {
        return 0;
    }

    /*
     * Protección visual/física:
     * si el siguiente paso cae en otro LED físico ocupado
     * por otro barco, se queda esperando.
     */
    int current_slot = canal_position_to_led_slot(current_pos);
    int next_slot = canal_position_to_led_slot(next_pos);

    if (current_slot < 0 || next_slot < 0) {
        return 0;
    }

    /*
     * Si sigue dentro del mismo segmento físico, puede avanzar.
     * Si va a entrar a otro segmento físico, ese segmento debe estar libre.
     */
    if (next_slot != current_slot &&
        canal_visual_slot_has_other_ship(next_slot, ship)) {
        return 0;
    }

    /*
     * Tomar semáforo de la siguiente posición lógica.
     */
    if (!sim_sem_wait(&g_canal.sem_positions[next_pos], ship->thread)) {
        return 0;
    }

    /*
     * Liberar posición actual.
     */
    g_canal.positions[current_pos] = NULL;
    sim_sem_signal(&g_canal.sem_positions[current_pos]);

    /*
     * Ocupar nueva posición.
     */
    g_canal.positions[next_pos] = ship;
    ship->position = next_pos;

    /*
     * Si con este movimiento llegó a la salida, finaliza.
     */
    if (canal_ship_has_finished(ship)) {
        canal_finish_ship(ship);
        return 0;
    }

    return 1;
}

static void canal_move_ship(Ship *ship)
{
    if (!ship || !ship->thread) {
        return;
    }

    if (ship->state != SHIP_CROSSING) {
        return;
    }

    if (!canal_valid_position(ship->position)) {
        return;
    }

    int steps = ship->speed;

    if (steps <= 0) {
        steps = 1;
    }

    /*
     * Avance por velocidad:
     * NORMAL  -> 1 paso lógico
     * FISHER  -> 2 pasos lógicos
     * PATROL  -> 3 pasos lógicos
     *
     * Cada paso se valida individualmente para evitar:
     * - rebase
     * - colisión lógica
     * - superposición visual
     * - acceso fuera de arreglo
     */
    int moved = 0;

    for (int i = 0; i < steps; i++) {
        if (!canal_advance_one_position(ship)) {
            break;
        }

        moved++;

        if (!ship->thread || ship->thread->state == THREAD_DONE) {
            return;
        }

        if (ship->state == SHIP_DONE) {
            return;
        }
    }

    /*
     * Si no logró moverse porque había otro barco adelante,
     * no se le descuenta remaining_ms. Está esperando recurso.
     */
    if (moved <= 0) {
        return;
    }

    SystemConfig *cfg = config_get();

    ship->remaining_ms -= cfg->canal_move_interval_ms;

    if (ship->remaining_ms < 0) {
        ship->remaining_ms = 0;
    }

    ship->thread->remaining_ms = ship->remaining_ms;

}

void canal_tick(void)
{
    static int elapsed_ms = 0;

    SystemConfig *cfg = config_get();

    if (cfg->flow_algo == FLOW_LETRERO && g_letrero_counting && g_letrero_open) {
        g_letrero_elapsed_ms += cfg->system_tick_ms;

        if (g_letrero_elapsed_ms >= cfg->letrero_ms) {
            g_letrero_open = 0;
            g_letrero_counting = 0;
        }
    }

    if (g_canal.interrupted) {
        return;
    }

    elapsed_ms += cfg->system_tick_ms;

    if (elapsed_ms < cfg->canal_move_interval_ms) {
        return;
    }

    elapsed_ms = 0;

    if (g_canal.ship_count <= 0) {
        canal_update_flow_when_empty();
        return;
    }

    if (g_canal.active_dir == CANAL_DIR_LEFT_TO_RIGHT) {
        for (int pos = g_canal.length - 1; pos >= 0; pos--) {
            Ship *ship = g_canal.positions[pos];
            if (ship) {
                canal_move_ship(ship);
            }
        }
    } else if (g_canal.active_dir == CANAL_DIR_RIGHT_TO_LEFT) {
        for (int pos = 0; pos < g_canal.length; pos++) {
            Ship *ship = g_canal.positions[pos];
            if (ship) {
                canal_move_ship(ship);
            }
        }
    }

    canal_update_flow_when_empty();
}

int canal_get_ship_count(void)
{
    return g_canal.ship_count;
}

int canal_get_length(void)
{
    return g_canal.length;
}

int canal_get_max_ships(void)
{
    return g_canal.max_ships;
}

CanalDirection canal_get_active_dir(void)
{
    return g_canal.active_dir;
}

Ship *canal_get_ship_at_position(int position)
{
    if (!canal_valid_position(position)) {
        return NULL;
    }

    return g_canal.positions[position];
}

void canal_set_interrupted(int value)
{
    g_canal.interrupted = value ? 1 : 0;
}

int canal_is_interrupted(void)
{
    return g_canal.interrupted;
}

const char *canal_dir_name(CanalDirection dir)
{
    switch (dir) {
        case CANAL_DIR_FREE:          return "FREE";
        case CANAL_DIR_LEFT_TO_RIGHT: return "L_TO_R";
        case CANAL_DIR_RIGHT_TO_LEFT: return "R_TO_L";
        default:                      return "UNKNOWN";
    }
}

const char *canal_flow_name(FlowAlgo algo)
{
    switch (algo) {
        case FLOW_TICO:    return "TICO";
        case FLOW_EQUIDAD: return "EQUIDAD";
        case FLOW_LETRERO: return "LETRERO";
        default:           return "UNKNOWN";
    }
}

void canal_format_flow_status(char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) {
        return;
    }

    SystemConfig *cfg = config_get();

    snprintf(buffer,
             buffer_size,
             "FLOW ALGO=%s ACTIVE=%s EQ_TURN=%s EQ_COUNT=%d W=%d LETRERO_DIR=%s LETRERO_OPEN=%d LETRERO_ELAPSED=%d LETRERO_MS=%d TICO_TURN=%s TICO_CLOSED=%d",
             canal_flow_name(cfg->flow_algo),
             canal_dir_name(g_canal.active_dir),
             canal_dir_name(g_equidad_turn_dir),
             g_equidad_passed_in_turn,
             cfg->equidad_w,
             canal_dir_name(g_letrero_dir),
             g_letrero_open,
             g_letrero_elapsed_ms,
             cfg->letrero_ms,
             canal_dir_name(g_tico_turn_dir),
             g_tico_batch_closed);
}

void canal_format_status(char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) {
        return;
    }

    SystemConfig *cfg = config_get();

    snprintf(buffer,
             buffer_size,
             "CANAL LEN=%d MAX=%d COUNT=%d DIR=%s FLOW=%s INTERRUPTED=%d MOVE_MS=%d",
             g_canal.length,
             g_canal.max_ships,
             g_canal.ship_count,
             canal_dir_name(g_canal.active_dir),
             canal_flow_name(cfg->flow_algo),
             g_canal.interrupted,
             cfg->canal_move_interval_ms);
}
