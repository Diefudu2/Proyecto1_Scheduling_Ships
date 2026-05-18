# Documentación interna - Scheduling Ships

## Arquitectura general

FreeRTOS ejecuta una tarea principal (`task_project_core`). Los barcos no son tareas FreeRTOS: son estructuras `Ship` asociadas a `SimThread`, administradas por la biblioteca propia de hilos y el scheduler del proyecto.

## Modelo de recursos

- Canal lógico: arreglo `positions[]` en `canal.c`.
- Canal físico: 10 LEDs de representación visual.
- Cada posición lógica se protege con `sem_positions[]`.
- El cupo global del canal se protege con `sem_cpu_slots`.

## Calendarizadores

No apropiativos:

- FCFS
- SJF
- PRIORITY

Apropiativos:

- RR: apropia por vencimiento de quantum.
- STRN: apropia cuando un proceso con menor tiempo restante tiene derecho al recurso.
- EDF: apropia cuando un proceso con deadline más urgente tiene derecho al recurso.

## Apropiación

La apropiación no permite rebase ni colisión. Cuando un barco apropiativo necesita tomar el recurso:

1. El barco bloqueador sale del canal.
2. Su posición lógica se guarda en `saved_position`.
3. Se libera su semáforo de posición.
4. Se libera cupo del canal.
5. El barco vuelve a READY.
6. Se aplica `preempt_cooldown_ms` para impedir que restaure inmediatamente y vuelva a bloquear.
7. Cuando sea calendarizado de nuevo, intenta restaurarse en `saved_position` si está libre.

## Razón del cooldown

Sin cooldown, el barco apropiado podía salir del canal y volver a entrar en la misma posición antes de que el barco apropiativo avanzara. Eso hacía que EDF/STRN/RR parecieran no apropiativos. El cooldown da una ventana temporal para que el barco ganador ocupe el recurso liberado.

## Restricciones de seguridad

- No se accede fuera de `positions[]`.
- Ningún barco ocupa una posición lógica ocupada.
- Ningún barco entra a un segmento físico/LED ocupado.
- No hay rebase: si la siguiente posición está ocupada, el barco espera o se apropia el bloqueador si el algoritmo lo permite.
