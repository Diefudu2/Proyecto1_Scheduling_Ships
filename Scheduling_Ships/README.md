# Scheduling Ships ESP32-C6

Simulación de planificación de barcos con ESP32-C6, FreeRTOS, una biblioteca propia de hilos simulados, políticas de calendarización y políticas de flujo del canal.

## Estructura del proyecto

```text
Scheduling_Ships/
├── CMakeLists.txt
├── Makefile
├── Ships/
│   ├── main.c
│   ├── canal.c / canal.h
│   ├── scheduler.c / scheduler.h
│   ├── ships.c / ships.h
│   ├── thread.c / thread.h
│   ├── semaphore.c / semaphore.h
│   ├── serial_protocol.c / serial_protocol.h
│   ├── interrupt_control.c / interrupt_control.h
│   ├── led_view.c / led_view.h
│   ├── config.c / config.h
│   ├── scenario.c / scenario.h
│   └── CMakeLists.txt
├── GUI/
│   ├── gui.py
│   └── serial_client.py
└── docs/
    ├── INTERNAL_DOCUMENTATION.md
    └── INSTALL.md
```

## Requisitos para compilar en otra computadora

### Firmware ESP32-C6

Instalar:

- Git
- Python 3
- ESP-IDF compatible con ESP32-C6
- Driver USB serial correspondiente a la placa
- Dependencia ESP-IDF `led_strip` de Espressif

En Linux, típicamente también se usan:

```bash
sudo apt update
sudo apt install -y git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
```

Después de instalar ESP-IDF, activar el entorno antes de compilar:

```bash
source ~/esp/esp-idf/export.sh
```

Compilar y cargar:

```bash
make run PORT=/dev/ttyACM0
```

Si el puerto cambia:

```bash
make run PORT=/dev/ttyUSB0
```

### GUI Python

Instalar `pyserial`:

```bash
pip3 install pyserial
```

Ejecutar:

```bash
make gui
```

o directamente:

```bash
python3 GUI/gui.py
```

## Comandos seriales principales

```text
START
PAUSE
STEP
RESET
STATUS
CONFIG
THREADS
SCHED
CANAL
FLOW
SNAPSHOT
INTERRUPT ON
INTERRUPT OFF
INTERRUPT TOGGLE
INTERRUPT STATUS
```

Crear barcos:

```text
n  Normal izquierda -> derecha
f  Pesquero izquierda -> derecha
p  Patrulla izquierda -> derecha
N  Normal derecha -> izquierda
F  Pesquero derecha -> izquierda
P  Patrulla derecha -> izquierda
```

## Configuración rápida

```text
CONFIG SCHED=EDF
CONFIG FLOW=EQUIDAD
CONFIG W=3
CONFIG CANAL_LENGTH=30
CONFIG MAX_IN_CANAL=10
CONFIG MOVE_MS=600
START
```

## Notas de diseño

- La interrupción tiene prioridad sobre todo.
- El canal impide choques, rebases y sentidos opuestos simultáneos.
- El flujo decide qué dirección puede entrar.
- El scheduler decide qué barco entra dentro de la dirección permitida.
- Los apropiativos RR, STRN y EDF solo deben apropiarse cuando hay disputa real de recurso.
