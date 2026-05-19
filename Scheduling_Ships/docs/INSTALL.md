# Instalación y puesta en marcha

## 1. Instalar ESP-IDF

Siga la instalación oficial de ESP-IDF para ESP32-C6. Después, active el entorno:

```bash
source ~/esp/esp-idf/export.sh
```

Verifique:

```bash
idf.py --version
```

## 2. Preparar el proyecto

Desde la carpeta raíz del proyecto:

```bash
make target
make build
```

## 3. Cargar al ESP32-C6

```bash
make flash PORT=/dev/ttyACM0
```

## 4. Monitor serial

```bash
make monitor PORT=/dev/ttyACM0
```

## 5. GUI

Instale pyserial:

```bash
pip3 install pyserial
```

Ejecute:

```bash
python3 GUI/gui.py
```

## 6. Problemas frecuentes

### Puerto no encontrado

Probar:

```bash
ls /dev/ttyACM* /dev/ttyUSB*
```

### Permiso denegado

Agregar el usuario al grupo `dialout`:

```bash
sudo usermod -a -G dialout $USER
```

Cerrar sesión y volver a entrar.

### Falta pyserial

```bash
pip3 install pyserial
```

### LED strip no encontrado

Asegurarse de que `led_strip` esté disponible como dependencia ESP-IDF.
