#include "hardware_leds.h"

#include <stddef.h>

/* ---------------------------------------------------------
 * Apaga todos los LEDs
 * --------------------------------------------------------- */
void hardware_leds_clear(int leds[HW_LED_COUNT])
{
    if (!leds) return;

    for (int i = 0; i < HW_LED_COUNT; i++) {
        leds[i] = LED_OFF;
    }
}

/* ---------------------------------------------------------
 * Convierte tipo de barco al codigo que entiende el ESP32-D
 * --------------------------------------------------------- */
int hardware_leds_ship_type_to_code(ShipType type)
{
    switch (type) {
        case SHIP_NORMAL:
            return LED_SHIP_NORMAL;

        case SHIP_FISHER:
            return LED_SHIP_FISHING;

        case SHIP_PATROL:
            return LED_SHIP_PATROL;

        default:
            return LED_OFF;
    }
}

/* ---------------------------------------------------------
 * Copia una cola de barcos a un segmento de LEDs.
 *
 * IMPORTANTE:
 * Esta funcion asume que canal->mutex ya esta tomado.
 * En el canal nuevo, ShipQueue no tiene mutex propio.
 * --------------------------------------------------------- */
static void map_queue_to_leds_unlocked(ShipQueue *queue,
                                       int leds[HW_LED_COUNT],
                                       int start_index,
                                       int end_index)
{
    if (!queue || !leds) return;

    Ship *cur = queue->head;
    int led_index = start_index;

    while (cur && led_index <= end_index) {
        leds[led_index] = hardware_leds_ship_type_to_code(cur->type);
        cur = cur->next;
        led_index++;
    }
}

/* ---------------------------------------------------------
 * Convierte posicion real del canal a posicion LED
 *
 * El canal puede tener longitud configurable, pero el
 * hardware solo tiene 10 LEDs para representarlo.
 * --------------------------------------------------------- */
static int canal_position_to_led(int position, int canal_length)
{
    if (canal_length <= 0) {
        canal_length = LED_CANAL_COUNT;
    }

    if (position <= 0) {
        position = 1;
    }

    if (position > canal_length) {
        position = canal_length;
    }

    int relative = ((position - 1) * LED_CANAL_COUNT) / canal_length;

    if (relative < 0) {
        relative = 0;
    }

    if (relative >= LED_CANAL_COUNT) {
        relative = LED_CANAL_COUNT - 1;
    }

    return LED_CANAL_START + relative;
}

/* ---------------------------------------------------------
 * Funcion principal:
 * convierte el estado actual del canal en una lista de 21 LEDs
 *
 * Mapa fisico:
 *   0 - 3    cola izquierda
 *   4        barrera izquierda
 *   5 - 14   canal
 *   15       barrera derecha
 *   16 - 19  cola derecha
 *   20       indicador de flujo
 * --------------------------------------------------------- */
void hardware_leds_from_canal(Canal *canal,
                              int leds[HW_LED_COUNT])
{
    if (!leds) return;

    hardware_leds_clear(leds);

    if (!canal) {
        return;
    }

    pthread_mutex_lock(&canal->mutex);

    /* Colas */
    map_queue_to_leds_unlocked(&canal->state.queue_left,
                               leds,
                               LED_LEFT_QUEUE_START,
                               LED_LEFT_QUEUE_END);

    map_queue_to_leds_unlocked(&canal->state.queue_right,
                               leds,
                               LED_RIGHT_QUEUE_START,
                               LED_RIGHT_QUEUE_END);

    /* Barcos dentro del canal */
    for (int i = 0; i < canal->state.ship_count; i++) {
        Ship *ship = canal->state.ships[i];

        if (!ship) {
            continue;
        }

        int led_index = canal_position_to_led(ship->pos,
                                              canal->config.canal_length);

        if (led_index >= LED_CANAL_START &&
            led_index <= LED_CANAL_END) {
            leds[led_index] = hardware_leds_ship_type_to_code(ship->type);
        }
    }

    /* Barreras / interrupcion */
    if (canal->state.interrupted) {
        leds[LED_LEFT_BARRIER]  = LED_INTERRUPTION;
        leds[LED_RIGHT_BARRIER] = LED_INTERRUPTION;
    } else if (canal->state.ship_count > 0 ||
               canal->state.direction != CANAL_DIR_FREE) {
        leds[LED_LEFT_BARRIER]  = LED_BARRIER_ACTIVE;
        leds[LED_RIGHT_BARRIER] = LED_BARRIER_ACTIVE;
    } else {
        leds[LED_LEFT_BARRIER]  = LED_OFF;
        leds[LED_RIGHT_BARRIER] = LED_OFF;
    }

    /* Indicador de flujo */
    if (canal->state.direction == CANAL_DIR_LEFT) {
        leds[LED_FLOW_INDICATOR] = LED_FLOW_RIGHT;
    } else if (canal->state.direction == CANAL_DIR_RIGHT) {
        leds[LED_FLOW_INDICATOR] = LED_FLOW_LEFT;
    } else {
        leds[LED_FLOW_INDICATOR] = LED_OFF;
    }

    pthread_mutex_unlock(&canal->mutex);
}