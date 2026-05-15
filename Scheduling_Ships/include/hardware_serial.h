#ifndef HARDWARE_SERIAL_H
#define HARDWARE_SERIAL_H

#include <stddef.h>

#define HW_LED_COUNT 21

typedef struct {
    int enabled;              /* 1 = usar hardware, 0 = ignorar */
    int connected;            /* 1 = puerto abierto correctamente */
    int fd;                   /* file descriptor del puerto serial */
    int baudrate;             /* normalmente 115200 */
    char port[128];           /* ejemplo: /dev/ttyUSB0 */
    char last_error[256];     /* ultimo error legible */
} HardwareSerial;

/*
 * Inicializa la estructura y abre el puerto serial si enabled == 1.
 *
 * Retorna:
 *   0  -> exito o hardware deshabilitado
 *  -1  -> error al abrir/configurar puerto
 */
int hardware_serial_init(HardwareSerial *hw,
                         const char *port,
                         int baudrate,
                         int enabled);

/*
 * Envia una lista de 21 valores al ESP32-D con el formato:
 *
 * [0,1,2,3,0,...]\n
 *
 * Este formato coincide con el parser del codigo del ESP32-D.
 *
 * Retorna:
 *   0  -> enviado correctamente
 *  -1  -> no enviado
 */
int hardware_serial_send_leds(HardwareSerial *hw,
                              const int leds[HW_LED_COUNT]);

/*
 * Envia una linea cruda al ESP32-D.
 * Util para pruebas manuales.
 *
 * Ejemplo:
 * hardware_serial_send_line(&hw, "[1,2,3,0,...]");
 */
int hardware_serial_send_line(HardwareSerial *hw,
                              const char *line);

/*
 * Lee una linea desde el ESP32-D, si hay datos disponibles.
 * Es no bloqueante.
 *
 * Puede servir luego si el ESP32-D envia eventos del boton.
 *
 * Retorna:
 *   >0 -> cantidad de caracteres leidos
 *    0 -> no habia datos disponibles
 *   -1 -> error
 */
int hardware_serial_read_line(HardwareSerial *hw,
                              char *buffer,
                              size_t buffer_size);

/*
 * Cierra el puerto serial si estaba abierto.
 */
void hardware_serial_close(HardwareSerial *hw);

/*
 * Retorna texto de estado:
 *   "disabled"
 *   "connected"
 *   "disconnected"
 */
const char *hardware_serial_status(const HardwareSerial *hw);

#endif