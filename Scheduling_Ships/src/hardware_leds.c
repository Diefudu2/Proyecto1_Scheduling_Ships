#include "hardware_leds.h"

#include <pthread.h>
#include <string.h>

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

        case SHIP_FISHING:
            return LED_SHIP_FISHING;

        case SHIP_PATROL:
            return LED_SHIP_PATROL;

        default:
            return LED_OFF;
    }
}

/* ---------------------------------------------------------
 * Copia una cola de barcos a un segmento de LEDs
 *
 * Ejemplo:
 *   Cola izquierda: LEDs 0 - 3
 *   Cola derecha:   LEDs 16 - 19
 * --------------------------------------------------------- */
static void map_queue_to_leds(ShipQueue *queue,
                              int leds[HW_LED_COUNT],
                              int start_index,
                              int end_index)
{
    if (!queue || !leds) return;

    pthread_mutex_lock(&queue->lock);

    Ship *cur = queue->head;
    int led_index = start_index;

    while (cur && led_index <= end_index) {
        leds[led_index] = hardware_leds_ship_type_to_code(cur->type);
        cur = cur->next;
        led_index++;
    }

    pthread_mutex_unlock(&queue->lock);
}

/* ---------------------------------------------------------
 * Convierte posicion real del canal a posicion LED
 *
 * El canal del programa puede tener longitud configurable,
 * pero el hardware solo tiene 10 LEDs para representar canal.
 *
 * Si canal_length = 10:
 *   posicion 1 -> LED 5
 *   posicion 10 -> LED 14
 *
 * Si canal_length es mayor o menor, se escala a 10 LEDs.
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
 * Mapea los barcos dentro del canal a LEDs 5 - 14
 * --------------------------------------------------------- */
static void map_canal_to_leds(Canal *canal,
                              int leds[HW_LED_COUNT])
{
    if (!canal || !leds) return;

    pthread_mutex_lock(&canal->lock);

    for (int i = 0; i < canal->in_canal_count; i++) {
        Ship *ship = canal->in_canal[i];

        if (!ship) {
            continue;
        }

        int led_index = canal_position_to_led(ship->position,
                                              canal->cfg.canal_length);

        if (led_index >= LED_CANAL_START &&
            led_index <= LED_CANAL_END) {
            leds[led_index] = hardware_leds_ship_type_to_code(ship->type);
        }
    }

    pthread_mutex_unlock(&canal->lock);
}

/* ---------------------------------------------------------
 * Mapea direccion actual del canal al LED 20
 *
 * Segun la asignacion:
 *   LED 20 = indicador de flujo
 *
 * CANAL_DIR_LEFT en el codigo actual representa:
 *   IZQ -> DER
 *
 * CANAL_DIR_RIGHT representa:
 *   DER -> IZQ
 * --------------------------------------------------------- */
static void map_flow_to_led(Canal *canal,
                            int leds[HW_LED_COUNT])
{
    if (!canal || !leds) return;

    pthread_mutex_lock(&canal->lock);

    CanalDirection dir = canal->current_dir;

    if (dir == CANAL_DIR_LEFT) {
        leds[LED_FLOW_INDICATOR] = LED_FLOW_RIGHT;
    } else if (dir == CANAL_DIR_RIGHT) {
        leds[LED_FLOW_INDICATOR] = LED_FLOW_LEFT;
    } else {
        leds[LED_FLOW_INDICATOR] = LED_OFF;
    }

    pthread_mutex_unlock(&canal->lock);
}

/* ---------------------------------------------------------
 * Mapea barreras / agujas
 *
 * Por ahora:
 *   - Si hay barcos en el canal, ambas barreras se muestran activas.
 *   - Si el canal esta libre, se apagan.
 *
 * Luego, cuando agreguemos interrupcion:
 *   - Si interruption_active == 1, se usara LED_INTERRUPTION.
 * --------------------------------------------------------- */
static void map_barriers_to_leds(Canal *canal,
                                 int leds[HW_LED_COUNT])
{
    if (!canal || !leds) return;

    pthread_mutex_lock(&canal->lock);

    if (canal->in_canal_count > 0 ||
        canal->current_dir != CANAL_DIR_FREE) {
        leds[LED_LEFT_BARRIER]  = LED_BARRIER_ACTIVE;
        leds[LED_RIGHT_BARRIER] = LED_BARRIER_ACTIVE;
    } else {
        leds[LED_LEFT_BARRIER]  = LED_OFF;
        leds[LED_RIGHT_BARRIER] = LED_OFF;
    }

    pthread_mutex_unlock(&canal->lock);
}

/* ---------------------------------------------------------
 * Funcion principal:
 * convierte el estado actual del canal en una lista de 21 LEDs
 *
 * Mapa:
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

    map_queue_to_leds(&canal->queue_left,
                      leds,
                      LED_LEFT_QUEUE_START,
                      LED_LEFT_QUEUE_END);

    map_queue_to_leds(&canal->queue_right,
                      leds,
                      LED_RIGHT_QUEUE_START,
                      LED_RIGHT_QUEUE_END);

    map_canal_to_leds(canal, leds);

    map_barriers_to_leds(canal, leds);

    map_flow_to_led(canal, leds);
}