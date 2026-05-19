/* ============================================================
 * Archivo: canal.c
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Implementa el canal lógico, control de entrada/salida, movimiento, interrupciones, restauración post-interrupción y políticas de flujo TICO/LETRERO/EQUIDAD.
 *
 * Documentación interna:
 * - La política de flujo se evalúa antes del scheduler para decidir qué sentido puede entrar.
 * - La interrupción no debe considerarse un vaciado normal del canal; por eso conserva sentido y saved_position.
 * - Los algoritmos apropiativos solo deben tomar recursos cuando existe disputa real por posición o segmento físico.
 *
 * Convenciones:
 * - Las funciones públicas se declaran en el .h correspondiente.
 * - Las funciones static son utilidades internas del archivo.
 * - Retornos int usan 1=éxito/verdadero y 0=fallo/falso salvo que se indique otra cosa.
 * ============================================================ */
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
 *
 * Reglas corregidas:
 * - Interrupción NO rota flujo. Evacúa y luego restaura sentido/posición.
 * - LETRERO solo cierra si hay demanda del lado contrario.
 * - TICO no implementa flujo adicional: decide scheduler + restricción física.
 * - EQUIDAD cambia tras W solo si el otro lado tiene demanda.
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
static Ship *canal_first_ship_in_visual_slot(int slot, Ship *self);
static int canal_has_waiting_in_dir(CanalDirection dir);
static int canal_flow_allows_entry(Ship *ship);
static void canal_update_flow_when_empty(void);
static int canal_preemptive_ship_beats(SchedAlgo algo, Ship *behind, Ship *front);
static int canal_try_preempt_blocker_for_ship(Ship *ship, Ship *blocker);

/* =========================================================
 * Estado de flujo
 * ========================================================= */

static CanalDirection g_equidad_turn_dir = CANAL_DIR_LEFT_TO_RIGHT;
static int g_equidad_passed_in_turn = 0;

static CanalDirection g_letrero_dir = CANAL_DIR_LEFT_TO_RIGHT;
static int g_letrero_elapsed_ms = 0;
static int g_letrero_open = 1;
static int g_letrero_counting = 0;

/* TICO queda como sin política adicional. Se conserva para status. */
static CanalDirection g_tico_turn_dir = CANAL_DIR_LEFT_TO_RIGHT;
static int g_tico_batch_closed = 0;

/* =========================================================
 * Estado de reanudación post-interrupción
 * ========================================================= */

static int g_resume_after_interrupt = 0;
static int g_resume_pending = 0;
static CanalDirection g_resume_dir = CANAL_DIR_FREE;

/* =========================================================
 * Utilidades básicas
 * ========================================================= */

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
    return (dir == CANAL_DIR_RIGHT_TO_LEFT)
           ? DIR_RIGHT_TO_LEFT
           : DIR_LEFT_TO_RIGHT;
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

/* =========================================================
 * Mapeo lógico -> LED físico
 * ========================================================= */

int canal_position_to_led_slot(int position)
{
    if (g_canal.length <= 0) {
        return -1;
    }

    if (position < 0 || position >= g_canal.length) {
        return -1;
    }

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
    return canal_first_ship_in_visual_slot(slot, self) != NULL;
}

static Ship *canal_first_ship_in_visual_slot(int slot, Ship *self)
{
    if (slot < 0 || slot >= LED_CANAL_COUNT) {
        return NULL;
    }

    for (int pos = 0; pos < g_canal.length; pos++) {
        Ship *other = g_canal.positions[pos];

        if (!other || other == self) {
            continue;
        }

        if (canal_position_to_led_slot(pos) == slot) {
            return other;
        }
    }

    return NULL;
}

/* =========================================================
 * Flujo
 * ========================================================= */

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

    g_resume_after_interrupt = 0;
    g_resume_pending = 0;
    g_resume_dir = CANAL_DIR_FREE;
}

static void canal_update_flow_when_empty(void)
{
    SystemConfig *cfg = config_get();

    if (g_canal.interrupted) {
        return;
    }

    if (g_resume_after_interrupt && g_resume_pending > 0) {
        return;
    }

    if (g_canal.ship_count != 0) {
        return;
    }

    g_canal.active_dir = CANAL_DIR_FREE;

    if (cfg->flow_algo == FLOW_LETRERO) {
        CanalDirection opposite = canal_opposite_dir(g_letrero_dir);

        if (!g_letrero_open) {
            if (canal_has_waiting_in_dir(opposite)) {
                g_letrero_dir = opposite;
            }

            g_letrero_elapsed_ms = 0;
            g_letrero_counting = 0;
            g_letrero_open = 1;
            return;
        }

        if (!canal_has_waiting_in_dir(g_letrero_dir) &&
            canal_has_waiting_in_dir(opposite)) {
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

        if (g_equidad_passed_in_turn >= cfg->equidad_w &&
            canal_has_waiting_in_dir(opposite)) {
            g_equidad_turn_dir = opposite;
            g_equidad_passed_in_turn = 0;
            return;
        }

        if (!canal_has_waiting_in_dir(g_equidad_turn_dir) &&
            canal_has_waiting_in_dir(opposite)) {
            g_equidad_turn_dir = opposite;
            g_equidad_passed_in_turn = 0;
        }
    }
    else {
        /* FLOW_TICO: sin política adicional. No alterna ni cierra lotes. */
        g_tico_batch_closed = 0;
    }
}

static int canal_flow_allows_entry(Ship *ship)
{
    if (!ship) {
        return 0;
    }

    SystemConfig *cfg = config_get();
    CanalDirection desired_dir = ship_dir_to_canal_dir(ship->dir);

    /*
     * Reanudación post-interrupción:
     * Mientras haya barcos evacuados pendientes, restauran primero
     * los del mismo sentido y con saved_position válida.
     * No se evalúa EQUIDAD/LETRERO/TICO durante esta fase.
     */
    if (g_resume_after_interrupt && g_resume_pending > 0) {
        if (desired_dir != g_resume_dir) {
            return 0;
        }

        if (!canal_valid_position(ship->saved_position)) {
            return 0;
        }

        return 1;
    }

    switch (cfg->flow_algo) {
        case FLOW_EQUIDAD: {
            if (cfg->equidad_w <= 0) {
                return 1;
            }

            if (g_equidad_turn_dir == CANAL_DIR_FREE) {
                g_equidad_turn_dir = desired_dir;
                g_equidad_passed_in_turn = 0;
            }

            CanalDirection opposite = canal_opposite_dir(g_equidad_turn_dir);

            if (!canal_has_waiting_in_dir(g_equidad_turn_dir) &&
                canal_has_waiting_in_dir(opposite)) {
                g_equidad_turn_dir = opposite;
                g_equidad_passed_in_turn = 0;
            }

            if (desired_dir != g_equidad_turn_dir) {
                return 0;
            }

            opposite = canal_opposite_dir(g_equidad_turn_dir);

            if (g_equidad_passed_in_turn >= cfg->equidad_w &&
                canal_has_waiting_in_dir(opposite)) {
                return 0;
            }

            return 1;
        }

        case FLOW_LETRERO: {
            if (g_letrero_dir == CANAL_DIR_FREE) {
                g_letrero_dir = desired_dir;
                g_letrero_elapsed_ms = 0;
                g_letrero_counting = 0;
                g_letrero_open = 1;
            }

            if (desired_dir != g_letrero_dir) {
                return 0;
            }

            if (!g_letrero_open) {
                CanalDirection opposite = canal_opposite_dir(g_letrero_dir);

                if (canal_has_waiting_in_dir(opposite)) {
                    return 0;
                }

                /* No hay demanda contraria: no bloquear artificialmente. */
                g_letrero_open = 1;
                g_letrero_elapsed_ms = 0;
                g_letrero_counting = 0;
            }

            return 1;
        }

        case FLOW_TICO:
        default:
            /* TICO = sin flujo adicional. Solo rige la restricción física del canal. */
            return 1;
    }
}

/* =========================================================
 * Inicialización / configuración
 * ========================================================= */

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

/* =========================================================
 * Entrada / salida del canal
 * ========================================================= */

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

    if (g_canal.active_dir != CANAL_DIR_FREE &&
        g_canal.active_dir != desired_dir) {
        return 0;
    }

    if (!canal_flow_allows_entry(ship)) {
        return 0;
    }

    int restoring_from_saved = canal_valid_position(ship->saved_position);
    int was_restoring_after_interrupt = 0;
    int entry_pos = restoring_from_saved
                    ? ship->saved_position
                    : canal_entry_position_for_ship(ship);

    if (!canal_valid_position(entry_pos)) {
        return 0;
    }

    if (g_resume_after_interrupt &&
        g_resume_pending > 0 &&
        restoring_from_saved &&
        desired_dir == g_resume_dir) {
        was_restoring_after_interrupt = 1;
    }

    if (g_canal.positions[entry_pos] != NULL) {
        return 0;
    }

    int entry_slot = canal_position_to_led_slot(entry_pos);

    if (entry_slot < 0 || entry_slot >= LED_CANAL_COUNT) {
        return 0;
    }

    if (canal_visual_slot_has_other_ship(entry_slot, ship)) {
        return 0;
    }

    if (!sim_sem_wait(&g_canal.sem_cpu_slots, ship->thread)) {
        return 0;
    }

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
    ship->thread->saved_position = -1;

    thread_set_running(ship->thread);

    SystemConfig *cfg = config_get();

    /* Restaurar un barco evacuado no cuenta como barco nuevo del turno. */
    if (!was_restoring_after_interrupt && !restoring_from_saved) {
        if (cfg->flow_algo == FLOW_EQUIDAD &&
            desired_dir == g_equidad_turn_dir) {
            g_equidad_passed_in_turn++;
        }
    }

    if (cfg->flow_algo == FLOW_LETRERO &&
        desired_dir == g_letrero_dir) {
        g_letrero_counting = 1;
    }

    /* TICO no cierra lotes ni alterna uno-y-uno. */
    g_tico_batch_closed = 0;

    if (was_restoring_after_interrupt) {
        if (g_resume_pending > 0) {
            g_resume_pending--;
        }

        if (g_resume_pending <= 0) {
            g_resume_after_interrupt = 0;
            g_resume_pending = 0;
            g_resume_dir = CANAL_DIR_FREE;
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
    ship->saved_position = -1;
    ship->state = SHIP_DONE;

    if (ship->thread) {
        ship->thread->saved_position = -1;
        thread_exit(ship->thread);
    }

    canal_update_flow_when_empty();
}

/* =========================================================
 * Interrupción
 * ========================================================= */

int canal_interrupt_activate(void)
{
    int removed = 0;

    if (g_canal.interrupted) {
        return 0;
    }

    g_resume_dir = g_canal.active_dir;
    g_resume_after_interrupt = 0;
    g_resume_pending = 0;

    g_canal.interrupted = 1;

    for (int pos = 0; pos < g_canal.length; pos++) {
        Ship *ship = g_canal.positions[pos];

        if (!ship || !ship->thread) {
            continue;
        }

        if (g_resume_dir == CANAL_DIR_FREE) {
            g_resume_dir = ship_dir_to_canal_dir(ship->dir);
        }

        ship->saved_position = pos;
        ship->thread->saved_position = pos;

        g_canal.positions[pos] = NULL;
        sim_sem_signal(&g_canal.sem_positions[pos]);
        sim_sem_signal(&g_canal.sem_cpu_slots);

        if (g_canal.ship_count > 0) {
            g_canal.ship_count--;
        }

        ship->position = -1;
        ship->state = SHIP_READY;

        thread_preempt(ship->thread);
        scheduler_add_ready(ship->thread);

        removed++;
    }

    if (removed > 0 && g_resume_dir != CANAL_DIR_FREE) {
        g_resume_after_interrupt = 1;
        g_resume_pending = removed;
        g_canal.active_dir = g_resume_dir;
    }

    return removed;
}

void canal_interrupt_deactivate(void)
{
    g_canal.interrupted = 0;

    if (g_resume_after_interrupt &&
        g_resume_pending > 0 &&
        g_resume_dir != CANAL_DIR_FREE) {
        g_canal.active_dir = g_resume_dir;
    }
}

/* =========================================================
 * Apropiación
 * ========================================================= */

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

    ship->saved_position = pos;
    ship->thread->saved_position = pos;

    g_canal.positions[pos] = NULL;
    sim_sem_signal(&g_canal.sem_positions[pos]);
    sim_sem_signal(&g_canal.sem_cpu_slots);

    if (g_canal.ship_count > 0) {
        g_canal.ship_count--;
    }

    ship->position = -1;
    ship->state = SHIP_READY;

    thread_preempt(ship->thread);
    scheduler_add_ready(ship->thread);

    canal_update_flow_when_empty();

    return 1;
}

int canal_has_crossing_ships(void)
{
    return g_canal.ship_count > 0;
}

static int canal_preemptive_ship_beats(SchedAlgo algo, Ship *behind, Ship *front)
{
    if (!behind || !front || !behind->thread || !front->thread) {
        return 0;
    }

    switch (algo) {
        case SCHED_RR:
            return front->thread->quantum_used_ms >= config_get()->quantum_ms;

        case SCHED_STRN:
            return behind->remaining_ms < front->remaining_ms;

        case SCHED_EDF:
            return behind->deadline_ms < front->deadline_ms;

        case SCHED_PRIORITY:
        case SCHED_FCFS:
        case SCHED_SJF:
        default:
            return 0;
    }
}

static int canal_try_preempt_blocker_for_ship(Ship *ship, Ship *blocker)
{
    if (!ship || !blocker || blocker == ship) {
        return 0;
    }

    SchedAlgo algo = scheduler_get_algorithm();

    if (algo != SCHED_RR && algo != SCHED_STRN && algo != SCHED_EDF) {
        return 0;
    }

    if (!canal_preemptive_ship_beats(algo, ship, blocker)) {
        return 0;
    }

    /*
     * Apropiación localizada:
     * solo se ejecuta cuando ship realmente está disputando el recurso
     * ocupado por blocker. No se apropia desde READY de forma global.
     */
    if (canal_preempt_ship(blocker)) {
        scheduler_note_preemption();
        return 1;
    }

    return 0;
}

int canal_preempt_blocker_for_edf(void)
{
    return canal_preempt_blocker_for_algo(SCHED_EDF);
}

int canal_preempt_blocker_for_algo(SchedAlgo algo)
{
    if (g_canal.ship_count <= 1) {
        return 0;
    }

    if (algo != SCHED_RR && algo != SCHED_STRN && algo != SCHED_EDF) {
        return 0;
    }

    if (g_canal.active_dir == CANAL_DIR_LEFT_TO_RIGHT) {
        for (int pos = 0; pos < g_canal.length - 1; pos++) {
            Ship *behind = g_canal.positions[pos];

            if (!behind || !behind->thread) {
                continue;
            }

            for (int front_pos = pos + 1; front_pos < g_canal.length; front_pos++) {
                Ship *front = g_canal.positions[front_pos];

                if (!front || !front->thread) {
                    continue;
                }

                if (canal_preemptive_ship_beats(algo, behind, front)) {
                    return canal_preempt_ship(front);
                }

                break;
            }
        }
    }
    else if (g_canal.active_dir == CANAL_DIR_RIGHT_TO_LEFT) {
        for (int pos = g_canal.length - 1; pos > 0; pos--) {
            Ship *behind = g_canal.positions[pos];

            if (!behind || !behind->thread) {
                continue;
            }

            for (int front_pos = pos - 1; front_pos >= 0; front_pos--) {
                Ship *front = g_canal.positions[front_pos];

                if (!front || !front->thread) {
                    continue;
                }

                if (canal_preemptive_ship_beats(algo, behind, front)) {
                    return canal_preempt_ship(front);
                }

                break;
            }
        }
    }

    return 0;
}

/* =========================================================
 * Movimiento
 * ========================================================= */

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

    if (canal_ship_has_finished(ship)) {
        canal_finish_ship(ship);
        return 0;
    }

    int next_pos = canal_next_position_for_ship(ship);

    if (!canal_valid_position(next_pos)) {
        canal_finish_ship(ship);
        return 0;
    }

    Ship *logical_blocker = g_canal.positions[next_pos];

    if (logical_blocker != NULL && logical_blocker != ship) {
        if (!canal_try_preempt_blocker_for_ship(ship, logical_blocker)) {
            return 0;
        }
    }

    if (g_canal.positions[next_pos] != NULL) {
        return 0;
    }

    int current_slot = canal_position_to_led_slot(current_pos);
    int next_slot = canal_position_to_led_slot(next_pos);

    if (current_slot < 0 || next_slot < 0) {
        return 0;
    }

    if (next_slot != current_slot) {
        Ship *visual_blocker = canal_first_ship_in_visual_slot(next_slot, ship);

        if (visual_blocker) {
            if (!canal_try_preempt_blocker_for_ship(ship, visual_blocker)) {
                return 0;
            }
        }

        if (canal_visual_slot_has_other_ship(next_slot, ship)) {
            return 0;
        }
    }

    if (!sim_sem_wait(&g_canal.sem_positions[next_pos], ship->thread)) {
        return 0;
    }

    g_canal.positions[current_pos] = NULL;
    sim_sem_signal(&g_canal.sem_positions[current_pos]);

    g_canal.positions[next_pos] = ship;
    ship->position = next_pos;

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

    if (moved <= 0) {
        return;
    }

    SystemConfig *cfg = config_get();

    /*
     * Tiempo consumido en el recurso canal.
     * RR usa este contador para permitir apropiación localizada cuando
     * otro barco intenta ocupar el recurso que este barco bloquea.
     */
    ship->thread->quantum_used_ms += cfg->canal_move_interval_ms;

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

    if (g_canal.interrupted) {
        return;
    }

    if (!(g_resume_after_interrupt && g_resume_pending > 0)) {
        if (cfg->flow_algo == FLOW_LETRERO &&
            g_letrero_counting &&
            g_letrero_open) {

            g_letrero_elapsed_ms += cfg->system_tick_ms;

            if (g_letrero_elapsed_ms >= cfg->letrero_ms) {
                CanalDirection opposite = canal_opposite_dir(g_letrero_dir);

                if (canal_has_waiting_in_dir(opposite)) {
                    g_letrero_open = 0;
                    g_letrero_counting = 0;
                } else {
                    g_letrero_elapsed_ms = 0;
                    g_letrero_counting = 1;
                    g_letrero_open = 1;
                }
            }
        }
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

/* =========================================================
 * Getters / formato
 * ========================================================= */

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
    if (value) {
        (void)canal_interrupt_activate();
    } else {
        canal_interrupt_deactivate();
    }
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
             "FLOW ALGO=%s ACTIVE=%s EQ_TURN=%s EQ_COUNT=%d W=%d LETRERO_DIR=%s LETRERO_OPEN=%d LETRERO_ELAPSED=%d LETRERO_MS=%d TICO_TURN=%s TICO_CLOSED=%d RESUME=%d RESUME_PENDING=%d RESUME_DIR=%s",
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
             g_tico_batch_closed,
             g_resume_after_interrupt,
             g_resume_pending,
             canal_dir_name(g_resume_dir));
}

void canal_format_status(char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) {
        return;
    }

    SystemConfig *cfg = config_get();

    snprintf(buffer,
             buffer_size,
             "CANAL LEN=%d MAX=%d COUNT=%d DIR=%s FLOW=%s INTERRUPTED=%d MOVE_MS=%d RESUME=%d RESUME_PENDING=%d RESUME_DIR=%s",
             g_canal.length,
             g_canal.max_ships,
             g_canal.ship_count,
             canal_dir_name(g_canal.active_dir),
             canal_flow_name(cfg->flow_algo),
             g_canal.interrupted,
             cfg->canal_move_interval_ms,
             g_resume_after_interrupt,
             g_resume_pending,
             canal_dir_name(g_resume_dir));
}
