# Corrección final de interrupción

Este paquete corrige el error de compilación por `canal_interrupt_activate()` y `canal_interrupt_deactivate()` no declaradas/definidas.

También conserva la corrección de reanudación post-interrupción:

- La interrupción evacua barcos del canal y los devuelve a READY.
- Se guarda `saved_position`.
- No se rota Equidad/Letrero/Tico por el vaciado artificial del canal.
- Al liberar la interrupción, los barcos del mismo sentido pueden restaurar primero.
- El sensor de fotoresistencia trabaja por estados para evitar bloqueo constante.

## Archivos relevantes

- `canal.c`
- `canal.h`
- `interrupt_control.c`
- `interrupt_control.h`
- `main.c`
- `serial_protocol.c`
- `CMakeLists.txt`
