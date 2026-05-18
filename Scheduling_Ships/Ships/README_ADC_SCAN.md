# Corrección del sensor: ADC scan

Si `RAW=1` con luz y tapado, el problema no es el umbral: se está leyendo el GPIO/canal equivocado o la señal no llega a un ADC.

## Archivos incluidos

- `interrupt_control.c`
- `interrupt_control.h`
- `CMakeLists.txt`

## Comandos nuevos

```text
INTERRUPT RAW
INTERRUPT SCAN
INTERRUPT GPIO=<num>
INTERRUPT CAL LIGHT
INTERRUPT CAL DARK
INTERRUPT THRESHOLD DARK=<num> LIGHT=<num>
INTERRUPT DARK_IS_LOW=0|1
INTERRUPT STABLE=<num>
```

## Prueba obligatoria

Con luz:

```text
INTERRUPT SCAN
```

Tapado:

```text
INTERRUPT SCAN
```

Busque el GPIO cuyo `RAW` cambie. Luego configúrelo:

```text
INTERRUPT GPIO=<gpio_que_cambia>
INTERRUPT RAW
```

Si ningún GPIO cambia, el problema es físico:
- pin equivocado,
- divisor mal cableado,
- falta resistencia de 10 kΩ,
- no hay tierra común,
- el pin de la placa no corresponde al GPIO esperado.

## Cableado recomendado

```text
3.3V ---- LDR ---- GPIO_ADC ---- resistencia 10kΩ ---- GND
```

Con ese cableado normalmente:
- luz -> RAW alto
- tapado -> RAW bajo

Entonces:

```text
INTERRUPT DARK_IS_LOW=1
```

## Calibración

Con luz:

```text
INTERRUPT CAL LIGHT
```

Tapado:

```text
INTERRUPT CAL DARK
```

Luego ajuste manualmente si hace falta:

```text
INTERRUPT THRESHOLD DARK=900 LIGHT=1800
```
