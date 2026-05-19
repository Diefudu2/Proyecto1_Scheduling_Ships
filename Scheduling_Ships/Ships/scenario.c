/* ============================================================
 * Archivo: scenario.c
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Carga escenarios iniciales o comandos de barcos desde cadenas de tokens n/f/p/N/F/P.
 *
 * Documentación interna:
 * - Mantener este módulo pequeño, con validaciones defensivas y sin asumir entradas válidas.
 *
 * Convenciones:
 * - Las funciones públicas se declaran en el .h correspondiente.
 * - Las funciones static son utilidades internas del archivo.
 * - Retornos int usan 1=éxito/verdadero y 0=fallo/falso salvo que se indique otra cosa.
 * ============================================================ */
#include "scenario.h"

#include "ships.h"
#include "serial_protocol.h"
#include "led_view.h"

#include <string.h>

static void scenario_add_token(char token)
{
    ShipType type;
    ShipDir dir;

    switch (token) {
        case 'n':
            type = SHIP_NORMAL;
            dir = DIR_LEFT_TO_RIGHT;
            break;

        case 'f':
            type = SHIP_FISHER;
            dir = DIR_LEFT_TO_RIGHT;
            break;

        case 'p':
            type = SHIP_PATROL;
            dir = DIR_LEFT_TO_RIGHT;
            break;

        case 'N':
            type = SHIP_NORMAL;
            dir = DIR_RIGHT_TO_LEFT;
            break;

        case 'F':
            type = SHIP_FISHER;
            dir = DIR_RIGHT_TO_LEFT;
            break;

        case 'P':
            type = SHIP_PATROL;
            dir = DIR_RIGHT_TO_LEFT;
            break;

        default:
            return;
    }

    Ship *s = ship_create(type, dir);

    if (!s) {
        serial_protocol_send_line("WARN BOOT_SCENARIO_SHIP_SKIPPED");
    }
}

void scenario_load_sequence(const char *sequence)
{
    if (!sequence || sequence[0] == '\0') {
        serial_protocol_send_line("BOOT SCENARIO EMPTY");
        return;
    }

    for (int i = 0; sequence[i] != '\0'; i++) {
        char c = sequence[i];

        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ',') {
            continue;
        }

        scenario_add_token(c);
    }

    serial_protocol_send_line("OK BOOT SCENARIO LOADED");
    led_view_render_phase4();
}