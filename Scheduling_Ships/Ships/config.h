#ifndef CONFIG_H
#define CONFIG_H

#include "system_types.h"
#include <stdint.h>

/* ============================================================
 * Límites máximos de compilación
 * ============================================================ */

#define CONFIG_MAX_SIM_THREADS       64
#define CONFIG_MAX_SHIPS             64
#define CONFIG_MAX_CANAL_POSITIONS   100
#define CONFIG_MAX_READY_QUEUE       64

/* ============================================================
 * Configuración por defecto
 * ============================================================ */

#define DEFAULT_BOOT_SCENARIO_ENABLED  1

/*
 * Si está vacío: "" inicia sin barcos.
 * Si tiene tokens: "n n n n P N F P" crea esos barcos al iniciar.
 */
#define DEFAULT_BOOT_SCENARIO_SEQUENCE "p n n f P N F P"

#define DEFAULT_SYSTEM_TICK_MS          10

/* SCHED_FCFS, SCHED_RR, SCHED_PRIORITY, SCHED_SJF, SCHED_STRN, SCHED_EDF */
#define DEFAULT_SCHED_ALGO             SCHED_PRIORITY

/* FLOW_TICO, FLOW_EQUIDAD, FLOW_LETRERO */
#define DEFAULT_FLOW_ALGO              FLOW_EQUIDAD

#define DEFAULT_QUANTUM_MS             500
#define DEFAULT_LETRERO_MS             1000
#define DEFAULT_EQUIDAD_W              3

#define DEFAULT_CANAL_MOVE_INTERVAL_MS 600
#define DEFAULT_CANAL_LENGTH           30
#define DEFAULT_MAX_SHIPS_IN_CANAL     10
#define DEFAULT_QUEUE_VISIBLE          4
#define DEFAULT_MAX_QUEUE_PER_SIDE     4

/* ============================================================
 * USB Serial/JTAG
 * ============================================================ */

#define SERIAL_BAUDRATE              115200
#define SERIAL_RX_BUFFER_SIZE        1024
#define SERIAL_TX_BUFFER_SIZE        1024
#define SERIAL_LINE_SIZE             160

/* ============================================================
 * LEDs WS2812 / NeoPixel
 * ============================================================ */

#define LED_STRIP_GPIO               4
#define LED_STRIP_COUNT              21

/*
 * Mapa físico actual:
 * 0 - 3     cola izquierda visible
 * 4         barrera izquierda
 * 5 - 14    canal / CPU visual
 * 15        barrera derecha
 * 16 - 19   cola derecha visible
 * 20        indicador de flujo
 */
#define LED_LEFT_QUEUE_START         0
#define LED_LEFT_QUEUE_END           3

#define LED_LEFT_BARRIER             4

#define LED_CANAL_START              5
#define LED_CANAL_END                14
#define LED_CANAL_COUNT              10

#define LED_RIGHT_BARRIER            15

#define LED_RIGHT_QUEUE_START        16
#define LED_RIGHT_QUEUE_END          19

#define LED_FLOW_INDICATOR           20

/* ============================================================
 * Configuración viva del sistema
 * ============================================================ */

typedef struct {
    int system_tick_ms;

    int canal_length;              /* largo lógico del canal */
    int max_ships_in_canal;        /* cupos simultáneos */
    int queue_visible;             /* LEDs visibles por cola */
    int max_queue_per_side;        /* límite lógico READY por lado */

    int quantum_ms;
    int letrero_ms;
    int equidad_w;

    int canal_move_interval_ms;

    int boot_scenario_enabled;
    const char *boot_scenario_sequence;

    SchedAlgo sched_algo;
    FlowAlgo flow_algo;
} SystemConfig;

void config_init_defaults(void);
SystemConfig *config_get(void);

int config_set_quantum_ms(int value);
int config_set_letrero_ms(int value);
int config_set_equidad_w(int value);
int config_set_queue_visible(int value);
int config_set_max_queue_per_side(int value);
int config_set_canal_length(int value);
int config_set_max_ships_in_canal(int value);
int config_set_sched_algo(SchedAlgo algo);
int config_set_flow_algo(FlowAlgo algo);
int config_set_canal_move_interval_ms(int value);

#endif
