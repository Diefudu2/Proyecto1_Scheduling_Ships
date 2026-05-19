#ifndef SERIAL_PROTOCOL_H
#define SERIAL_PROTOCOL_H


/* ============================================================
 * Archivo: serial_protocol.h
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Declara la inicialización, polling y escritura de líneas del protocolo serial.
 *
 * Este encabezado contiene la API pública del módulo. Mantener aquí solo
 * tipos, constantes y prototipos requeridos por otros archivos.
 * ============================================================ */
void serial_protocol_init(void);
void serial_protocol_poll(void);
void serial_protocol_send_line(const char *line);

#endif