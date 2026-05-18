# GUI con botones de interrupción

Esta versión agrega panel de interrupción:

- `INT ON`
- `INT OFF`
- `TOGGLE`
- `STATUS`
- `SENSOR ON`
- `SENSOR OFF`

La fotoresistencia sigue funcionando en el firmware. Los botones de la GUI envían comandos seriales.

## Ejecutar

```bash
python3 GUI/gui.py
```

## Requisito

```bash
pip3 install pyserial
```

## Nota

Para que la GUI pinte las puertas en rojo, `SNAPSHOT` debe incluir `INT=0` o `INT=1`.
Si no está incluido, la GUI igual puede usar `INTERRUPT STATUS`, pero la actualización puede depender de la respuesta del comando.
