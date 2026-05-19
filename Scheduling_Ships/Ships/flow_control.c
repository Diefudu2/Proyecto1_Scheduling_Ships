/* ============================================================
 * Archivo: flow_control.c
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Reserva de módulo para futura separación de políticas de flujo; actualmente la fuente de verdad está en canal.c.
 *
 * Documentación interna:
 * - Mantener este módulo pequeño, con validaciones defensivas y sin asumir entradas válidas.
 *
 * Convenciones:
 * - Las funciones públicas se declaran en el .h correspondiente.
 * - Las funciones static son utilidades internas del archivo.
 * - Retornos int usan 1=éxito/verdadero y 0=fallo/falso salvo que se indique otra cosa.
 * ============================================================ */
/*
 * flow_control.c
 *
 * La lógica de flujo activa se implementa actualmente dentro de canal.c
 * para mantener una sola fuente de verdad sobre dirección, cupos y entrada
 * al canal. Este archivo queda como reserva para una futura separación del
 * módulo FLOW sin afectar la compilación si alguien lo agrega al CMake.
 */

void flow_control_init(void)
{
    /* No-op intencional. */
}
