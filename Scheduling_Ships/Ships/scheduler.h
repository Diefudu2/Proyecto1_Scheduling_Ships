#ifndef SCHEDULER_H
#define SCHEDULER_H


/* ============================================================
 * Archivo: scheduler.h
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Define la estructura Scheduler y la API para añadir, elegir, despachar y consultar hilos simulados.
 *
 * Este encabezado contiene la API pública del módulo. Mantener aquí solo
 * tipos, constantes y prototipos requeridos por otros archivos.
 * ============================================================ */
#include "thread.h"
#include "ships.h"
#include "system_types.h"

typedef struct {
    SchedAlgo algo;
    int quantum_ms;

    SimThread *ready_head;
    SimThread *ready_tail;
    int ready_count;

    SimThread *running;

    int enabled;
    int total_ticks;
    int total_preemptions;
    int total_finished;
} Scheduler;

void scheduler_init(void);
SimThread *scheduler_get_ready_head(void);
int scheduler_dispatch_to_canal(void);

void scheduler_set_algorithm(SchedAlgo algo);
SchedAlgo scheduler_get_algorithm(void);

void scheduler_start(void);
void scheduler_pause(void);
int scheduler_is_enabled(void);

void scheduler_add_ready(SimThread *t);
SimThread *scheduler_pick_next(void);

void scheduler_tick(void);
void scheduler_step_once(void);
int scheduler_apply_preemption(void);
void scheduler_note_preemption(void);

int scheduler_ready_count(void);
SimThread *scheduler_get_running(void);

const char *scheduler_algo_name(SchedAlgo algo);
int scheduler_parse_algo(const char *text, SchedAlgo *out);

void scheduler_print_ready_queue(char *buffer, int buffer_size);
void scheduler_print_status(char *buffer, int buffer_size);
int scheduler_has_ready_ship_dir(ShipDir dir);

#endif
