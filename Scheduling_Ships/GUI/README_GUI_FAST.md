# GUI rápida con SNAPSHOT

Esta versión evita pedir `STATUS + CANAL + THREADS + FLOW` en cada refresco.

## Recomendado

Agregar el comando `SNAPSHOT` al firmware con el parche incluido en:

```text
firmware_snapshot_patch.c
```

Luego la GUI consulta una sola línea cada 120 ms aproximadamente.

## Ejecutar

```bash
python3 GUI/gui.py
```

## Modo legado

Si todavía no agregó `SNAPSHOT`, desactive el check `SNAPSHOT` en la GUI. En ese modo consulta `CANAL`, `THREADS` y `STATUS` alternados, pero será menos fluido.

## Teclas

- `w`: cerrar elegantemente.
- `espacio`: STEP.
- `n f p N F P`: crear barcos.
