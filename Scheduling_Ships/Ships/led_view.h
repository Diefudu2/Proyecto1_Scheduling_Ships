#ifndef LED_VIEW_H
#define LED_VIEW_H


/* ============================================================
 * Archivo: led_view.h
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Expone funciones de inicialización, limpieza, prueba y renderizado de la vista LED.
 *
 * Este encabezado contiene la API pública del módulo. Mantener aquí solo
 * tipos, constantes y prototipos requeridos por otros archivos.
 * ============================================================ */
void led_view_init(void);
void led_view_clear(void);
void led_view_test_pattern(void);
void led_view_render_phase2(void);
void led_view_render_phase4(void);

#endif