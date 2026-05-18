# Fix apropiativos localizados

Este paquete corrige el comportamiento de RR, STRN y EDF sin tocar los no apropiativos.

## Cambio principal

Se eliminó la apropiación global desde READY contra barcos en el canal dentro de `scheduler_apply_preemption()`.

Antes:
- EDF podía sacar un NORMAL apenas una PATRULLA entraba al canal.
- STRN podía dejar el canal congelado por apropiaciones anticipadas.
- RR podía alterar el flujo al sacar barcos por quantum sin disputa física.

Ahora:
- La apropiación ocurre solo en `canal_advance_one_position()`.
- Es decir, solo cuando el barco intenta ocupar una posición lógica o segmento visual ocupado.
- No hay rebase ni colisión.
- El flujo no debe rotar por una apropiación anticipada.

## Archivos

Reemplace:

```text
Ships/canal.c
Ships/scheduler.c
```

Los `.h` se incluyen solo por conveniencia.

## Pruebas rápidas

### EDF

```text
RESET
CONFIG FLOW=TICO
CONFIG SCHED=EDF
CONFIG CANAL_LENGTH=30
CONFIG MAX_IN_CANAL=10
CONFIG MOVE_MS=600
START
n
```

Espere que el normal avance. Luego:

```text
p
```

Esperado:
- El normal NO debe salir del canal apenas entra `p`.
- La patrulla avanza hasta disputarle recurso.
- Solo cuando intente ocupar posición/segmento ocupado, el normal pasa a READY.

### STRN

```text
RESET
CONFIG FLOW=TICO
CONFIG SCHED=STRN
CONFIG CANAL_LENGTH=30
CONFIG MAX_IN_CANAL=10
CONFIG MOVE_MS=600
START
n
```

Espere 1 o 2 movimientos. Luego:

```text
p
```

Esperado:
- No se congela el canal.
- La patrulla solo apropia al normal cuando realmente lo alcanza o intenta entrar a su segmento.

### RR

```text
RESET
CONFIG FLOW=TICO
CONFIG SCHED=RR
CONFIG QUANTUM=1200
CONFIG CANAL_LENGTH=30
CONFIG MAX_IN_CANAL=10
CONFIG MOVE_MS=600
START
n
f
p
```

Esperado:
- RR no debe sacar barcos sin disputa.
- Si un barco bloquea a otro y ya consumió quantum, puede volver a READY.
- No debe alterar el sentido del flujo por sí solo.
