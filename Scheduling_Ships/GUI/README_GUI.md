# GUI Scheduling Ships

## Requisitos

```bash
pip3 install pyserial
```

En Ubuntu, si falta Tkinter:

```bash
sudo apt install python3-tk
```

## Ejecutar

Desde la raíz del proyecto:

```bash
python3 GUI/gui.py
```

## Importante

Esta GUI usa un solo hilo. La lectura serial y el refresco de pantalla se hacen con `tkinter.after()`.

## Refresco automático

La GUI consulta periódicamente:

```text
STATUS
CANAL
THREADS
FLOW
```

Por eso la vista lógica se actualiza aunque no se escriba nada en terminal.

## Teclas

- `w`: cerrar elegantemente la GUI.
- `espacio`: enviar STEP.
- `n f p N F P`: crear barcos.

## Escenarios

Los `.txt` van en:

```text
GUI/scenarios/
```

Ejemplo:

```text
p n n f P N F P
```

Acepta comas, saltos de línea y comentarios con `#`.
