#include "hardware_serial.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* ---------------------------------------------------------
 * Conversor de baudrate entero a constante POSIX termios
 * --------------------------------------------------------- */
static speed_t baudrate_to_termios(int baudrate)
{
    switch (baudrate) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
#ifdef B230400
        case 230400: return B230400;
#endif
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
        default:     return B115200;
    }
}

/* ---------------------------------------------------------
 * Guardar mensaje de error dentro de la estructura
 * --------------------------------------------------------- */
static void set_error(HardwareSerial *hw, const char *prefix)
{
    if (!hw) return;

    snprintf(hw->last_error,
             sizeof(hw->last_error),
             "%s: %s",
             prefix,
             strerror(errno));
}

/* ---------------------------------------------------------
 * Configuracion del puerto serial para ESP32-D
 *
 * Modo:
 *   8 bits de datos
 *   sin paridad
 *   1 bit de parada
 *   sin control de flujo
 *   lectura/escritura no bloqueante
 * --------------------------------------------------------- */
static int configure_port(HardwareSerial *hw)
{
    struct termios tty;

    if (tcgetattr(hw->fd, &tty) != 0) {
        set_error(hw, "tcgetattr");
        return -1;
    }

    speed_t speed = baudrate_to_termios(hw->baudrate);

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    /* Modo raw: evita interpretacion de caracteres */
    cfmakeraw(&tty);

    /* 8N1 */
    tty.c_cflag &= ~PARENB;          /* sin paridad */
    tty.c_cflag &= ~CSTOPB;          /* 1 stop bit */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;              /* 8 bits */

    /* Sin control de flujo por hardware */
    #ifdef CRTSCTS
        tty.c_cflag &= ~CRTSCTS;
    #endif

    /* Habilitar receptor y evitar que el puerto sea controlador terminal */
    tty.c_cflag |= CREAD | CLOCAL;

    /* Sin control de flujo por software */
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    /*
     * Lectura no bloqueante:
     * VMIN = 0  -> read puede retornar sin datos
     * VTIME = 0 -> sin espera
     */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(hw->fd, TCSANOW, &tty) != 0) {
        set_error(hw, "tcsetattr");
        return -1;
    }

    tcflush(hw->fd, TCIOFLUSH);
    return 0;
}

/* ---------------------------------------------------------
 * Inicializar comunicacion serial
 * --------------------------------------------------------- */
int hardware_serial_init(HardwareSerial *hw,
                         const char *port,
                         int baudrate,
                         int enabled)
{
    if (!hw) return -1;

    memset(hw, 0, sizeof(*hw));

    hw->enabled   = enabled ? 1 : 0;
    hw->connected = 0;
    hw->fd        = -1;
    hw->baudrate  = (baudrate > 0) ? baudrate : 115200;

    if (port && port[0] != '\0') {
        snprintf(hw->port, sizeof(hw->port), "%s", port);
    } else {
        snprintf(hw->port, sizeof(hw->port), "%s", "/dev/ttyUSB0");
    }

    snprintf(hw->last_error, sizeof(hw->last_error), "sin errores");

    if (!hw->enabled) {
        return 0;
    }

    /*
     * O_NOCTTY: el puerto no se vuelve terminal de control.
     * O_NONBLOCK: evita que el programa se congele si el ESP32-D no responde.
     */
    hw->fd = open(hw->port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (hw->fd < 0) {
        set_error(hw, "open serial port");
        hw->connected = 0;
        return -1;
    }

    if (configure_port(hw) != 0) {
        close(hw->fd);
        hw->fd = -1;
        hw->connected = 0;
        return -1;
    }

    /*
     * Muchos ESP32 se reinician al abrir el puerto serial.
     * Este retardo pequeño permite que el firmware termine de arrancar.
     */
    usleep(1500000);

    tcflush(hw->fd, TCIOFLUSH);

    hw->connected = 1;
    return 0;
}

/* ---------------------------------------------------------
 * Enviar linea cruda al ESP32-D
 * --------------------------------------------------------- */
int hardware_serial_send_line(HardwareSerial *hw,
                              const char *line)
{
    if (!hw || !line) return -1;

    if (!hw->enabled) {
        return 0;
    }

    if (!hw->connected || hw->fd < 0) {
        snprintf(hw->last_error,
                 sizeof(hw->last_error),
                 "hardware serial no conectado");
        return -1;
    }

    char buffer[512];

    /*
     * El firmware del ESP32-D usa readStringUntil('\n'),
     * por eso siempre garantizamos salto de linea.
     */
    int len = snprintf(buffer, sizeof(buffer), "%s\n", line);
    if (len < 0 || len >= (int)sizeof(buffer)) {
        snprintf(hw->last_error,
                 sizeof(hw->last_error),
                 "linea serial demasiado larga");
        return -1;
    }

    ssize_t written = write(hw->fd, buffer, (size_t)len);
    if (written < 0) {
        set_error(hw, "write serial");
        hw->connected = 0;
        return -1;
    }

    if (written != len) {
        snprintf(hw->last_error,
                 sizeof(hw->last_error),
                 "write serial incompleto");
        return -1;
    }

    tcdrain(hw->fd);
    return 0;
}

/* ---------------------------------------------------------
 * Enviar lista de LEDs
 * --------------------------------------------------------- */
int hardware_serial_send_leds(HardwareSerial *hw,
                              const int leds[HW_LED_COUNT])
{
    if (!hw || !leds) return -1;

    if (!hw->enabled) {
        return 0;
    }

    char line[256];
    int offset = 0;

    offset += snprintf(line + offset, sizeof(line) - (size_t)offset, "[");

    for (int i = 0; i < HW_LED_COUNT; i++) {
        int value = leds[i];

        /*
         * El ESP32-D acepta enteros. Limitamos por seguridad
         * a los codigos actualmente usados por el firmware:
         * 0..7.
         */
        if (value < 0) value = 0;
        if (value > 7) value = 7;

        offset += snprintf(line + offset,
                           sizeof(line) - (size_t)offset,
                           "%d%s",
                           value,
                           (i == HW_LED_COUNT - 1) ? "" : ",");

        if (offset < 0 || offset >= (int)sizeof(line)) {
            snprintf(hw->last_error,
                     sizeof(hw->last_error),
                     "lista de LEDs demasiado larga");
            return -1;
        }
    }

    offset += snprintf(line + offset, sizeof(line) - (size_t)offset, "]");

    if (offset < 0 || offset >= (int)sizeof(line)) {
        snprintf(hw->last_error,
                 sizeof(hw->last_error),
                 "lista de LEDs demasiado larga");
        return -1;
    }

    return hardware_serial_send_line(hw, line);
}

/* ---------------------------------------------------------
 * Leer linea desde ESP32-D, no bloqueante
 * --------------------------------------------------------- */
int hardware_serial_read_line(HardwareSerial *hw,
                              char *buffer,
                              size_t buffer_size)
{
    if (!hw || !buffer || buffer_size == 0) return -1;

    buffer[0] = '\0';

    if (!hw->enabled) {
        return 0;
    }

    if (!hw->connected || hw->fd < 0) {
        snprintf(hw->last_error,
                 sizeof(hw->last_error),
                 "hardware serial no conectado");
        return -1;
    }

    size_t pos = 0;

    while (pos + 1 < buffer_size) {
        char ch;
        ssize_t n = read(hw->fd, &ch, 1);

        if (n > 0) {
            if (ch == '\n') break;
            if (ch == '\r') continue;

            buffer[pos++] = ch;
        } else if (n == 0) {
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            set_error(hw, "read serial");
            hw->connected = 0;
            return -1;
        }
    }

    buffer[pos] = '\0';
    return (int)pos;
}

/* ---------------------------------------------------------
 * Cerrar comunicacion serial
 * --------------------------------------------------------- */
void hardware_serial_close(HardwareSerial *hw)
{
    if (!hw) return;

    if (hw->fd >= 0) {
        close(hw->fd);
    }

    hw->fd = -1;
    hw->connected = 0;
}

/* ---------------------------------------------------------
 * Estado textual
 * --------------------------------------------------------- */
const char *hardware_serial_status(const HardwareSerial *hw)
{
    if (!hw) return "invalid";
    if (!hw->enabled) return "disabled";
    if (hw->connected) return "connected";
    return "disconnected";
}