#ifndef INTERRUPT_CONTROL_H
#define INTERRUPT_CONTROL_H

#include <stddef.h>

/*
 * interrupt_control.h
 *
 * Control de interrupción por fotoresistencia + GUI.
 *
 * Esta versión mantiene compatibilidad con el código actual del proyecto:
 *
 *   main.c:
 *     interrupt_control_poll();
 *
 *   serial_protocol.c:
 *     interrupt_control_reset();
 *     interrupt_control_format_status(...);
 *     interrupt_control_manual_on();
 *     interrupt_control_manual_off();
 *     interrupt_control_manual_toggle();
 *     interrupt_control_sensor_enable(...);
 *     interrupt_control_sensor_rearm();
 *
 * También conserva:
 *
 *   interrupt_control_tick();
 *   interrupt_control_handle_command(...);
 */

void interrupt_control_init(void);
void interrupt_control_reset(void);

void interrupt_control_tick(void);
void interrupt_control_poll(void);

int interrupt_control_is_active(void);

void interrupt_control_manual_on(void);
void interrupt_control_manual_off(void);
void interrupt_control_manual_toggle(void);

void interrupt_control_sensor_enable(int enabled);
void interrupt_control_sensor_rearm(void);

void interrupt_control_format_status(char *buffer, int buffer_size);

int interrupt_control_handle_command(const char *cmd);

#endif
