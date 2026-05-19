/* ============================================================
 * Archivo: led_view.c
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Renderiza la tira de LEDs: colas, canal comprimido, barreras, estado de interrupción y dirección de flujo.
 *
 * Documentación interna:
 * - El canal lógico se comprime a 10 LEDs físicos; no se debe dibujar doble ocupación sin indicarlo.
 * - Las barreras blancas parpadeantes indican abierto; rojo fijo indica interrupción.
 *
 * Convenciones:
 * - Las funciones públicas se declaran en el .h correspondiente.
 * - Las funciones static son utilidades internas del archivo.
 * - Retornos int usan 1=éxito/verdadero y 0=fallo/falso salvo que se indique otra cosa.
 * ============================================================ */
#include "led_view.h"
#include "config.h"
#include "ships.h"
#include "scheduler.h"
#include "canal.h"

#include "esp_log.h"
#include "led_strip.h"

#include <stdint.h>

static const char *TAG = "LED_VIEW";
static led_strip_handle_t led_strip;

static void led_view_color_for_ship(Ship *s, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = 0;
    *g = 0;
    *b = 0;

    if (!s) {
        return;
    }

    switch (s->type) {
        case SHIP_NORMAL:
            *g = 40;
            break;

        case SHIP_FISHER:
            *b = 40;
            break;

        case SHIP_PATROL:
            *r = 40;
            *b = 40;
            break;

        default:
            break;
    }
}


void led_view_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        }
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        }
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(
        &strip_config,
        &rmt_config,
        &led_strip
    ));

    led_view_clear();

    ESP_LOGI(TAG, "LED strip inicializada: GPIO=%d, LEDs=%d",
             LED_STRIP_GPIO,
             LED_STRIP_COUNT);
}

void led_view_clear(void)
{
    if (!led_strip) {
        return;
    }

    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
}

void led_view_test_pattern(void)
{
    if (!led_strip) {
        return;
    }

    led_strip_clear(led_strip);

    for (int i = LED_LEFT_QUEUE_START; i <= LED_LEFT_QUEUE_END; i++) {
        led_strip_set_pixel(led_strip, i, 0, 40, 0);
    }

    led_strip_set_pixel(led_strip, LED_LEFT_BARRIER, 40, 40, 40);
    led_strip_set_pixel(led_strip, LED_RIGHT_BARRIER, 40, 40, 40);

    for (int i = LED_CANAL_START; i <= LED_CANAL_END; i++) {
        led_strip_set_pixel(led_strip, i, 0, 0, 40);
    }

    for (int i = LED_RIGHT_QUEUE_START; i <= LED_RIGHT_QUEUE_END; i++) {
        led_strip_set_pixel(led_strip, i, 40, 0, 40);
    }

    led_strip_set_pixel(led_strip, LED_FLOW_INDICATOR, 40, 40, 0);
    led_strip_refresh(led_strip);
}

void led_view_render_phase2(void)
{
    led_view_render_phase4();
}

void led_view_render_phase4(void)
{
    if (!led_strip) {
        return;
    }

    led_strip_clear(led_strip);

    SystemConfig *cfg = config_get();

    int left_limit = LED_LEFT_QUEUE_START + cfg->queue_visible - 1;
    int right_limit = LED_RIGHT_QUEUE_START + cfg->queue_visible - 1;

    if (left_limit > LED_LEFT_QUEUE_END) {
        left_limit = LED_LEFT_QUEUE_END;
    }

    if (right_limit > LED_RIGHT_QUEUE_END) {
        right_limit = LED_RIGHT_QUEUE_END;
    }

    /*
     * 1. READY queue real.
     */
    int left_index = LED_LEFT_QUEUE_START;
    int right_index = LED_RIGHT_QUEUE_START;

    SimThread *cur = scheduler_get_ready_head();

    while (cur) {
        if (cur->arg && cur->state == THREAD_READY) {
            Ship *s = (Ship *)cur->arg;

            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;

            led_view_color_for_ship(s, &r, &g, &b);

            if (s->dir == DIR_LEFT_TO_RIGHT) {
                if (left_index <= left_limit) {
                    led_strip_set_pixel(led_strip, left_index, r, g, b);
                    left_index++;
                }
            } else {
                if (right_index <= right_limit) {
                    led_strip_set_pixel(led_strip, right_index, r, g, b);
                    right_index++;
                }
            }
        }

        cur = cur->next;
    }

    /*
     * 2. Canal lógico comprimido a LEDs físicos.
     *
     * Importante:
     * - Se recorre TODO el canal lógico.
     * - La conversión posición lógica -> LED físico la hace canal.c.
     * - Si aparece más de un barco en el mismo LED físico, se pinta rojo
     *   como ERROR visual/lógico, no como estado normal.
     */
    int led_occupied_count[LED_CANAL_COUNT];
    uint8_t led_r[LED_CANAL_COUNT];
    uint8_t led_g[LED_CANAL_COUNT];
    uint8_t led_b[LED_CANAL_COUNT];

    for (int i = 0; i < LED_CANAL_COUNT; i++) {
        led_occupied_count[i] = 0;
        led_r[i] = 0;
        led_g[i] = 0;
        led_b[i] = 0;
    }

    int canal_length = canal_get_length();

    if (canal_length > 0 && canal_length <= CONFIG_MAX_CANAL_POSITIONS) {
        for (int pos = 0; pos < canal_length; pos++) {
            Ship *s = canal_get_ship_at_position(pos);

            if (!s) {
                continue;
            }

            int led_index = canal_position_to_led_index(pos);

            if (led_index < LED_CANAL_START || led_index > LED_CANAL_END) {
                continue;
            }

            int offset = led_index - LED_CANAL_START;

            if (offset < 0 || offset >= LED_CANAL_COUNT) {
                continue;
            }

            led_occupied_count[offset]++;

            /*
             * Caso normal: solo un barco en este LED físico.
             */
            if (led_occupied_count[offset] == 1) {
                led_view_color_for_ship(s,
                                        &led_r[offset],
                                        &led_g[offset],
                                        &led_b[offset]);
            }

            /*
             * Caso anormal: dos barcos cayeron en el mismo LED.
             * Esto no debería pasar si canal.c bloquea correctamente
             * los segmentos físicos ocupados.
             */
            else {
                led_r[offset] = 40;
                led_g[offset] = 0;
                led_b[offset] = 0;
            }
        }
    }

    for (int i = 0; i < LED_CANAL_COUNT; i++) {
        if (led_occupied_count[i] > 0) {
            led_strip_set_pixel(led_strip,
                                LED_CANAL_START + i,
                                led_r[i],
                                led_g[i],
                                led_b[i]);
        }
    }

    /*
    * 3. Barreras / puertas.
    *
    * Blanco parpadeante = puertas abiertas, operación normal.
    * Rojo fijo          = puertas cerradas por interrupción.
    */
    static int blink_elapsed_ms = 0;
    static int blink_on = 1;

    blink_elapsed_ms += config_get()->system_tick_ms;

    if (blink_elapsed_ms >= 500) {
        blink_elapsed_ms = 0;
        blink_on = !blink_on;
    }

    if (canal_is_interrupted()) {
        led_strip_set_pixel(led_strip, LED_LEFT_BARRIER, 40, 0, 0);
        led_strip_set_pixel(led_strip, LED_RIGHT_BARRIER, 40, 0, 0);
    } else {
        if (blink_on) {
            led_strip_set_pixel(led_strip, LED_LEFT_BARRIER, 40, 40, 40);
            led_strip_set_pixel(led_strip, LED_RIGHT_BARRIER, 40, 40, 40);
        } else {
            led_strip_set_pixel(led_strip, LED_LEFT_BARRIER, 0, 0, 0);
            led_strip_set_pixel(led_strip, LED_RIGHT_BARRIER, 0, 0, 0);
        }
    }

    /*
     * 4. Indicador de dirección.
     */
    CanalDirection dir = canal_get_active_dir();

    if (dir == CANAL_DIR_LEFT_TO_RIGHT) {
        led_strip_set_pixel(led_strip, LED_FLOW_INDICATOR, 40, 40, 0);
    } else if (dir == CANAL_DIR_RIGHT_TO_LEFT) {
        led_strip_set_pixel(led_strip, LED_FLOW_INDICATOR, 0, 40, 40);
    } else {
        led_strip_set_pixel(led_strip, LED_FLOW_INDICATOR, 0, 0, 0);
    }

    led_strip_refresh(led_strip);
}