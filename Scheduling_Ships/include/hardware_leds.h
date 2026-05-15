#ifndef HARDWARE_LEDS_H
#define HARDWARE_LEDS_H

#include "canal.h"
#include "ship.h"

#define HW_LED_COUNT 21

/* Mapa fisico de la tira LED */
#define LED_LEFT_QUEUE_START    0
#define LED_LEFT_QUEUE_END      3

#define LED_LEFT_BARRIER        4

#define LED_CANAL_START         5
#define LED_CANAL_END           14
#define LED_CANAL_COUNT         10

#define LED_RIGHT_BARRIER       15

#define LED_RIGHT_QUEUE_START   16
#define LED_RIGHT_QUEUE_END     19

#define LED_FLOW_INDICATOR      20

/* Codigos que entiende el ESP32-D */
#define LED_OFF                 0
#define LED_SHIP_NORMAL         1
#define LED_SHIP_FISHING        2
#define LED_SHIP_PATROL         3
#define LED_BARRIER_ACTIVE      4
#define LED_INTERRUPTION        5
#define LED_FLOW_RIGHT          6
#define LED_FLOW_LEFT           7

void hardware_leds_clear(int leds[HW_LED_COUNT]);

int hardware_leds_ship_type_to_code(ShipType type);

void hardware_leds_from_canal(Canal *canal,
                              int leds[HW_LED_COUNT]);

#endif