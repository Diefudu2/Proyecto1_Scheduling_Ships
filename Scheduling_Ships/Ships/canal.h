#ifndef CANAL_H
#define CANAL_H

#include "config.h"
#include "ships.h"
#include "semaphore.h"

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

    SimSemaphore sem_cpu_slots;
    SimSemaphore sem_positions[CONFIG_MAX_CANAL_POSITIONS];
} Canal;

void canal_init(void);
int canal_apply_config(void);

int canal_try_enter(Ship *ship);
void canal_tick(void);

int canal_preempt_ship(Ship *ship);
int canal_preempt_blocker_for_algo(SchedAlgo algo);
int canal_has_crossing_ships(void);

int canal_get_ship_count(void);
int canal_get_length(void);
int canal_get_max_ships(void);
CanalDirection canal_get_active_dir(void);
Ship *canal_get_ship_at_position(int position);

int canal_position_to_led_slot(int position);
int canal_position_to_led_index(int position);

int canal_interrupt_activate(void);
void canal_interrupt_deactivate(void);

void canal_set_interrupted(int value);
int canal_is_interrupted(void);

const char *canal_dir_name(CanalDirection dir);
const char *canal_flow_name(FlowAlgo algo);
void canal_format_flow_status(char *buffer, int buffer_size);
void canal_format_status(char *buffer, int buffer_size);

#endif
