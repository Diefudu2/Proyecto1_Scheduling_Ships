#include "config.h"

static SystemConfig g_config;

static int config_min_int(int a, int b)
{
    return (a < b) ? a : b;
}

void config_init_defaults(void)
{
    g_config.system_tick_ms = DEFAULT_SYSTEM_TICK_MS;

    g_config.canal_length = DEFAULT_CANAL_LENGTH;
    g_config.max_ships_in_canal = DEFAULT_MAX_SHIPS_IN_CANAL;
    g_config.queue_visible = DEFAULT_QUEUE_VISIBLE;
    g_config.max_queue_per_side = DEFAULT_MAX_QUEUE_PER_SIDE;

    g_config.quantum_ms = DEFAULT_QUANTUM_MS;
    g_config.letrero_ms = DEFAULT_LETRERO_MS;
    g_config.equidad_w = DEFAULT_EQUIDAD_W;
    g_config.canal_move_interval_ms = DEFAULT_CANAL_MOVE_INTERVAL_MS;

    g_config.boot_scenario_enabled = DEFAULT_BOOT_SCENARIO_ENABLED;
    g_config.boot_scenario_sequence = DEFAULT_BOOT_SCENARIO_SEQUENCE;

    g_config.sched_algo = DEFAULT_SCHED_ALGO;
    g_config.flow_algo = DEFAULT_FLOW_ALGO;

    if (g_config.canal_length <= 0) {
        g_config.canal_length = 1;
    }

    if (g_config.canal_length > CONFIG_MAX_CANAL_POSITIONS) {
        g_config.canal_length = CONFIG_MAX_CANAL_POSITIONS;
    }

    if (g_config.max_ships_in_canal <= 0) {
        g_config.max_ships_in_canal = 1;
    }

    if (g_config.max_ships_in_canal > g_config.canal_length) {
        g_config.max_ships_in_canal = g_config.canal_length;
    }
}

SystemConfig *config_get(void)
{
    return &g_config;
}

int config_set_max_queue_per_side(int value)
{
    if (value <= 0 || value > CONFIG_MAX_SHIPS) {
        return 0;
    }

    g_config.max_queue_per_side = value;
    return 1;
}

int config_set_quantum_ms(int value)
{
    if (value <= 0) {
        return 0;
    }

    g_config.quantum_ms = value;
    return 1;
}

int config_set_letrero_ms(int value)
{
    if (value <= 0) {
        return 0;
    }

    g_config.letrero_ms = value;
    return 1;
}

int config_set_equidad_w(int value)
{
    if (value <= 0) {
        return 0;
    }

    g_config.equidad_w = value;
    return 1;
}

int config_set_canal_move_interval_ms(int value)
{
    if (value <= 0) {
        return 0;
    }

    g_config.canal_move_interval_ms = value;
    return 1;
}

int config_set_queue_visible(int value)
{
    if (value <= 0) {
        return 0;
    }

    int physical_left_slots = LED_LEFT_QUEUE_END - LED_LEFT_QUEUE_START + 1;
    int physical_right_slots = LED_RIGHT_QUEUE_END - LED_RIGHT_QUEUE_START + 1;
    int physical_limit = config_min_int(physical_left_slots, physical_right_slots);

    if (value > physical_limit) {
        return 0;
    }

    g_config.queue_visible = value;
    return 1;
}

int config_set_canal_length(int value)
{
    if (value <= 0 || value > CONFIG_MAX_CANAL_POSITIONS) {
        return 0;
    }

    g_config.canal_length = value;

    if (g_config.max_ships_in_canal > g_config.canal_length) {
        g_config.max_ships_in_canal = g_config.canal_length;
    }

    return 1;
}

int config_set_max_ships_in_canal(int value)
{
    if (value <= 0 || value > g_config.canal_length) {
        return 0;
    }

    g_config.max_ships_in_canal = value;
    return 1;
}

int config_set_sched_algo(SchedAlgo algo)
{
    if (algo < SCHED_FCFS || algo > SCHED_EDF) {
        return 0;
    }

    g_config.sched_algo = algo;
    return 1;
}

int config_set_flow_algo(FlowAlgo algo)
{
    if (algo < FLOW_TICO || algo > FLOW_LETRERO) {
        return 0;
    }

    g_config.flow_algo = algo;
    return 1;
}
