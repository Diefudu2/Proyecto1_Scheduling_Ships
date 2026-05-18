# Corrección de fotoresistencia / ADC

## Problema corregido

El estado mostraba `RAW=1` tanto con luz como tapado. Eso indica que no se estaba leyendo correctamente el canal ADC real o que el valor mostrado era equivalente a una lectura booleana/digital.

## Cambios

- `interrupt_control.c` usa `adc_oneshot_io_to_channel()` para convertir el GPIO físico al canal ADC correcto.
- `INTERRUPT STATUS` fuerza una nueva lectura ADC antes de imprimir el estado.
- El estado ahora muestra:
  - `RAW`: lectura ADC actual.
  - `CURRENT_TH`: el valor RAW actual, útil para escoger umbrales.
  - `DARK_TH`: umbral actual de oscuridad.
  - `LIGHT_TH`: umbral actual de luz.
  - `ADC_READY`, `READ_OK`, `ADC_UNIT`, `ADC_CHANNEL`.
  - `DARK_REF` y `LIGHT_REF` si se usa calibración.

## Comandos nuevos

```text
INTERRUPT RAW
INTERRUPT CAL LIGHT
INTERRUPT CAL DARK
INTERRUPT THRESHOLD DARK=<num> LIGHT=<num>
```

## Prueba recomendada

1. Con luz normal:

```text
INTERRUPT RAW
INTERRUPT CAL LIGHT
```

2. Tape la fotoresistencia:

```text
INTERRUPT RAW
INTERRUPT CAL DARK
```

3. Revise:

```text
INTERRUPT STATUS
```

Después de tener referencias de luz y oscuridad, el sistema calcula umbrales con histéresis.

## Cableado recomendado

```text
3.3V ---- LDR ---- GPIO_ADC ---- resistencia 10kΩ ---- GND
```

Con esa conexión normalmente:

```text
con luz -> RAW alto
tapado  -> RAW bajo
```

por lo que debe quedar:

```c
#define INTERRUPT_PHOTO_DARK_IS_LOW 1
```

Si el comportamiento es invertido, use:

```c
#define INTERRUPT_PHOTO_DARK_IS_LOW 0
```
