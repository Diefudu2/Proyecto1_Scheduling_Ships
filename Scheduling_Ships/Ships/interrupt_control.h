#ifndef INTERRUPT_CONTROL_H
#define INTERRUPT_CONTROL_H

/*
 * interrupt_control.h
 *
 * Control de interrupción externa del canal.
 *
 * Fuentes soportadas:
 * - Fotoresistencia: activa la interrupción cuando se tapa la luz y la
 *   libera cuando vuelve una iluminación suficiente.
 * - GUI / Serial: permite activar, liberar o alternar la interrupción con
 *   INTERRUPT ON, INTERRUPT OFF e INTERRUPT TOGGLE.
 *
 * No se usa botón físico. El botón manual esperado está en la interfaz
 * gráfica, por lo que se maneja por comandos seriales.
 *
 * La fotoresistencia trabaja por transición de estados, con histéresis y
 * confirmación por varias muestras. Esto evita que se envíe la misma señal
 * continuamente mientras el sensor permanece tapado.
 */

void interrupt_control_init(void);
void interrupt_control_poll(void);
void interrupt_control_reset(void);

void interrupt_control_manual_on(void);
void interrupt_control_manual_off(void);
void interrupt_control_manual_toggle(void);

void interrupt_control_sensor_enable(int enabled);
void interrupt_control_sensor_rearm(void);

int interrupt_control_is_active(void);
void interrupt_control_format_status(char *buffer, int buffer_size);

#endif
