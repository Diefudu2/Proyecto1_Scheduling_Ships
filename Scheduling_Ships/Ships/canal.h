#ifndef CANAL_H
#define CANAL_H


/* ============================================================
 * Archivo: canal.h
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Expone la estructura del canal, direcciones lógicas y API pública para entrada, avance, interrupción y diagnóstico del canal.
 *
 * CAMBIO DE FASE (tareas reales + sincronización real):
 * - Los SimSemaphore simulados se reemplazaron por objetos REALES de FreeRTOS.
 * - La protección del recurso "canal" usa un mutex recursivo (estructura) + un
 *   semáforo contador (cupos simultáneos) + 1 semáforo binario POR POSICIÓN
 *   lógica del canal (cada franja es un recurso), todos declarados como
 *   estáticos en canal.c.
 * - canal_lock()/canal_unlock() exponen la sección crítica del canal.
 * - canal_step_ship() es el paso de avance que ejecuta la TAREA REAL del barco.
 *
 * Este encabezado contiene la API pública del módulo. Mantener aquí solo
 * tipos, constantes y prototipos requeridos por otros archivos.
 * ============================================================ */
#include "config.h"
#include "ships.h"

#include <stddef.h>

typedef enum {
    CANAL_DIR_FREE = 0,
    CANAL_DIR_LEFT_TO_RIGHT,
    CANAL_DIR_RIGHT_TO_LEFT
} CanalDirection;

typedef struct {
    int length;
    int max_ships;

    CanalDirection active_dir;

    Ship *positions[CONFIG_MAX_CANAL_POSITIONS];

    int ship_count;
    int interrupted;
} Canal;

void canal_init(void);
int canal_apply_config(void);

/* Sección crítica del recurso "canal" (mutex recursivo real de FreeRTOS). */
void canal_lock(void);
void canal_unlock(void);

int canal_try_enter(Ship *ship);
void canal_tick(void);

/*
 * canal_step_ship:
 * Paso de avance ejecutado por la TAREA REAL del barco cuando el scheduler le
 * concede ejecución. Toma el mutex del canal, avanza el barco según su speed y
 * libera el mutex. No asume que el llamador ya tenga el lock.
 */
void canal_step_ship(Ship *ship);

int canal_preempt_ship(Ship *ship);
int canal_preempt_blocker_for_edf(void);
int canal_preempt_blocker_for_algo(SchedAlgo algo);
int canal_has_crossing_ships(void);

int canal_interrupt_activate(void);
void canal_interrupt_deactivate(void);

int canal_get_ship_count(void);
int canal_get_length(void);
int canal_get_max_ships(void);
CanalDirection canal_get_active_dir(void);
Ship *canal_get_ship_at_position(int position);

int canal_position_to_led_slot(int position);
int canal_position_to_led_index(int position);

void canal_set_interrupted(int value);
int canal_is_interrupted(void);

const char *canal_dir_name(CanalDirection dir);
const char *canal_flow_name(FlowAlgo algo);
void canal_format_flow_status(char *buffer, int buffer_size);
void canal_format_status(char *buffer, int buffer_size);

#endif