# Documentación interna - Interrupciones por fotoresistencia

## Propósito

La interrupción representa el evento de seguridad del canal: un buque externo se aproxima y las barreras deben cerrarse. El proyecto solicita que, si hay barcos dentro del canal, se retiren del canal y vuelvan a la cola de listos sin perder su estado.

## Fuentes de interrupción

Esta versión usa dos fuentes:

1. Fotoresistencia física.
2. Botón de la interfaz gráfica mediante comandos seriales.

No se usa botón físico en GPIO.

## Comandos disponibles

```text
INTERRUPT STATUS
INTERRUPT ON
INTERRUPT OFF
INTERRUPT TOGGLE
INTERRUPT SENSOR ON
INTERRUPT SENSOR OFF
INTERRUPT SENSOR REARM
```

`INTERRUPT ON/OFF/TOGGLE` corresponde al botón manual de la GUI.

## Máquina de estados del sensor

La fotoresistencia no envía señales constantes. El módulo usa histéresis y muestras estables:

```text
RAW <= DARK_TH   -> oscuridad estable, si DARK_IS_LOW=1
RAW >= LIGHT_TH  -> luz estable, si DARK_IS_LOW=1
```

Si el divisor está invertido, `INTERRUPT_PHOTO_DARK_IS_LOW` debe ponerse en `0`.

El sensor solo dispara cuando pasa de un estado armado con luz suficiente a oscuridad estable. Si el sistema arranca oscuro o si se desactiva manualmente mientras sigue oscuro, se activa `ACK_UNTIL_LIGHT`: no vuelve a disparar hasta que detecte luz suficiente.

## Evitar bloqueo constante

Campos relevantes del estado:

```text
SENSOR             interrupción actualmente solicitada por el sensor
ACK_UNTIL_LIGHT    espera luz antes de permitir otro disparo
SEEN_LIGHT         el sensor ya vio luz estable desde el último reset/rearm
RAW                última lectura ADC
```

Si el sistema queda bloqueado por un umbral incorrecto:

```text
INTERRUPT SENSOR OFF
```

Para rearmarlo:

```text
INTERRUPT SENSOR REARM
```

## Acción al activarse

Cuando la interrupción se activa:

1. `canal_interrupt_activate()` marca el canal interrumpido.
2. Los barcos dentro del canal se sacan con `canal_preempt_ship()`.
3. Cada barco vuelve a READY.
4. Conserva `saved_position` y `remaining_ms`.
5. Las barreras se muestran cerradas.

Cuando se libera:

1. `canal_interrupt_deactivate()` limpia el estado.
2. El scheduler vuelve a admitir barcos según el algoritmo y el flujo.
3. Los barcos apropiados intentan restaurarse en `saved_position` si el recurso está libre.
