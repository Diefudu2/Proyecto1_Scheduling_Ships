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
