#include "interrupt_control.h"

#include "config.h"
#include "canal.h"
#include "serial_protocol.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

/* ============================================================
 * interrupt_control.c
 *
 * Sensor de interrupción por fotoresistencia.
 *
 * Esta versión vuelve a la lógica simple que funcionaba en el
 * prototipo Arduino:
 *
 *   sensor apagado:
 *      si RAW cruza umbral de sombra estable -> SENSOR:1
 *
 *   sensor activo:
 *      si RAW cruza umbral de luz estable -> SENSOR:0
 *
 * Para el caso reportado:
 *   con luz: RAW ≈ 1281
 *   tapado: RAW ≈ 177
 *
 * valores recomendados:
 *   GPIO=2
 *   DARK_IS_LOW=1
 *   DARK_TH=300
 *   LIGHT_TH=800
 *   STABLE=5
 *
 * Además, esta versión incluye las funciones que su serial_protocol.c
 * y main.c ya están llamando para evitar errores de compilación.
 * ============================================================ */

#ifndef INTERRUPT_PHOTO_ADC_GPIO
#define INTERRUPT_PHOTO_ADC_GPIO          2
#endif

#ifndef INTERRUPT_PHOTO_DARK_THRESHOLD
#define INTERRUPT_PHOTO_DARK_THRESHOLD    300
#endif

#ifndef INTERRUPT_PHOTO_LIGHT_THRESHOLD
#define INTERRUPT_PHOTO_LIGHT_THRESHOLD   800
#endif

#ifndef INTERRUPT_PHOTO_DARK_IS_LOW
#define INTERRUPT_PHOTO_DARK_IS_LOW       1
#endif

#ifndef INTERRUPT_PHOTO_STABLE_SAMPLES
#define INTERRUPT_PHOTO_STABLE_SAMPLES    5
#endif

#ifndef INTERRUPT_PHOTO_SENSOR_ENABLED
#define INTERRUPT_PHOTO_SENSOR_ENABLED    1
#endif

#ifndef INTERRUPT_SENSOR_COOLDOWN_MS
#define INTERRUPT_SENSOR_COOLDOWN_MS      800
#endif

static int g_interrupt_active = 0;
static int g_manual_interrupt = 0;
static int g_sensor_interrupt = 0;

static int g_sensor_enabled = INTERRUPT_PHOTO_SENSOR_ENABLED;
static int g_photo_gpio = INTERRUPT_PHOTO_ADC_GPIO;

static int g_dark_threshold = INTERRUPT_PHOTO_DARK_THRESHOLD;
static int g_light_threshold = INTERRUPT_PHOTO_LIGHT_THRESHOLD;
static int g_dark_is_low = INTERRUPT_PHOTO_DARK_IS_LOW;
static int g_stable_samples = INTERRUPT_PHOTO_STABLE_SAMPLES;

static int g_dark_count = 0;
static int g_light_count = 0;

static int g_last_raw = -1;
static int g_last_read_ok = 0;

static uint32_t g_last_sensor_event_tick = 0;

static adc_oneshot_unit_handle_t g_adc_handle = NULL;
static adc_unit_t g_adc_unit = ADC_UNIT_1;
static adc_channel_t g_adc_channel = ADC_CHANNEL_0;
static int g_adc_ready = 0;

/* ============================================================
 * ADC
 * ============================================================ */

static void interrupt_adc_delete_unit(void)
{
    if (g_adc_handle) {
        adc_oneshot_del_unit(g_adc_handle);
        g_adc_handle = NULL;
    }

    g_adc_ready = 0;
}

static int interrupt_adc_configure_gpio(int gpio)
{
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;

    interrupt_adc_delete_unit();

    esp_err_t err = adc_oneshot_io_to_channel(gpio, &unit, &channel);

    if (err != ESP_OK) {
        char line[128];
        snprintf(line,
                 sizeof(line),
                 "ERR INTERRUPT GPIO_NOT_ADC GPIO=%d ESP_ERR=%d",
                 gpio,
                 (int)err);
        serial_protocol_send_line(line);
        return 0;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = unit,
    };

    err = adc_oneshot_new_unit(&unit_cfg, &g_adc_handle);

    if (err != ESP_OK) {
        char line[128];
        snprintf(line,
                 sizeof(line),
                 "ERR INTERRUPT ADC_UNIT_INIT GPIO=%d UNIT=%d ESP_ERR=%d",
                 gpio,
                 (int)unit,
                 (int)err);
        serial_protocol_send_line(line);
        g_adc_handle = NULL;
        return 0;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    err = adc_oneshot_config_channel(g_adc_handle, channel, &chan_cfg);

    if (err != ESP_OK) {
        char line[128];
        snprintf(line,
                 sizeof(line),
                 "ERR INTERRUPT ADC_CHAN_CONFIG GPIO=%d UNIT=%d CH=%d ESP_ERR=%d",
                 gpio,
                 (int)unit,
                 (int)channel,
                 (int)err);
        serial_protocol_send_line(line);
        interrupt_adc_delete_unit();
        return 0;
    }

    g_photo_gpio = gpio;
    g_adc_unit = unit;
    g_adc_channel = channel;
    g_adc_ready = 1;

    char line[128];
    snprintf(line,
             sizeof(line),
             "OK INTERRUPT ADC_READY GPIO=%d UNIT=%d CH=%d",
             g_photo_gpio,
             (int)g_adc_unit,
             (int)g_adc_channel);
    serial_protocol_send_line(line);

    return 1;
}

static int interrupt_photo_read_raw(void)
{
    int raw = -1;

    if (!g_adc_ready || !g_adc_handle) {
        g_last_raw = -1;
        g_last_read_ok = 0;
        return -1;
    }

    esp_err_t err = adc_oneshot_read(g_adc_handle, g_adc_channel, &raw);

    if (err != ESP_OK) {
        g_last_raw = -1;
        g_last_read_ok = 0;
        return -1;
    }

    g_last_raw = raw;
    g_last_read_ok = 1;

    return raw;
}

static int interrupt_raw_is_dark(int raw)
{
    if (raw < 0) {
        return 0;
    }

    if (g_dark_is_low) {
        return raw < g_dark_threshold;
    }

    return raw > g_dark_threshold;
}

static int interrupt_raw_is_light(int raw)
{
    if (raw < 0) {
        return 0;
    }

    if (g_dark_is_low) {
        return raw > g_light_threshold;
    }

    return raw < g_light_threshold;
}

static int interrupt_sensor_cooldown_elapsed(void)
{
    uint32_t now = (uint32_t)xTaskGetTickCount();
    uint32_t elapsed_ticks = now - g_last_sensor_event_tick;
    uint32_t cooldown_ticks = pdMS_TO_TICKS(INTERRUPT_SENSOR_COOLDOWN_MS);

    return elapsed_ticks >= cooldown_ticks;
}

/* ============================================================
 * Aplicación del estado global
 * ============================================================ */

static void interrupt_apply_state(void)
{
    int wanted = (g_manual_interrupt || g_sensor_interrupt) ? 1 : 0;

    if (wanted == g_interrupt_active) {
        return;
    }

    g_interrupt_active = wanted;

    if (g_interrupt_active) {
        int removed = canal_interrupt_activate();

        char line[96];
        snprintf(line,
                 sizeof(line),
                 "INTERRUPT ACTIVE=1 REMOVED=%d SOURCE=%s",
                 removed,
                 g_manual_interrupt ? "MANUAL" : "SENSOR");
        serial_protocol_send_line(line);
    } else {
        canal_interrupt_deactivate();
        serial_protocol_send_line("INTERRUPT ACTIVE=0");
    }
}

/* ============================================================
 * API pública
 * ============================================================ */

void interrupt_control_init(void)
{
    g_interrupt_active = 0;
    g_manual_interrupt = 0;
    g_sensor_interrupt = 0;

    g_sensor_enabled = INTERRUPT_PHOTO_SENSOR_ENABLED;
    g_photo_gpio = INTERRUPT_PHOTO_ADC_GPIO;

    g_dark_threshold = INTERRUPT_PHOTO_DARK_THRESHOLD;
    g_light_threshold = INTERRUPT_PHOTO_LIGHT_THRESHOLD;
    g_dark_is_low = INTERRUPT_PHOTO_DARK_IS_LOW;
    g_stable_samples = INTERRUPT_PHOTO_STABLE_SAMPLES;

    g_dark_count = 0;
    g_light_count = 0;

    g_last_raw = -1;
    g_last_read_ok = 0;
    g_last_sensor_event_tick = 0;

    if (!interrupt_adc_configure_gpio(g_photo_gpio)) {
        g_sensor_enabled = 0;
    }
}

void interrupt_control_reset(void)
{
    /*
     * Se usa después de RESET del sistema.
     * Reconfigura estado lógico del sensor sin perder la configuración ADC.
     */
    g_interrupt_active = 0;
    g_manual_interrupt = 0;
    g_sensor_interrupt = 0;
    g_dark_count = 0;
    g_light_count = 0;
    g_last_sensor_event_tick = (uint32_t)xTaskGetTickCount();
}

void interrupt_control_tick(void)
{
    if (!g_sensor_enabled || !g_adc_ready) {
        return;
    }

    int raw = interrupt_photo_read_raw();

    if (raw < 0) {
        return;
    }

    /*
     * Igual al prototipo funcional:
     * - sensor apagado: buscar sombra.
     * - sensor activo: buscar luz.
     */
    if (!g_sensor_interrupt) {
        if (interrupt_raw_is_dark(raw)) {
            g_dark_count++;
        } else {
            g_dark_count = 0;
        }

        if (g_dark_count >= g_stable_samples &&
            interrupt_sensor_cooldown_elapsed()) {

            g_sensor_interrupt = 1;
            g_dark_count = 0;
            g_light_count = 0;
            g_last_sensor_event_tick = (uint32_t)xTaskGetTickCount();

            serial_protocol_send_line("SENSOR:1");
            interrupt_apply_state();
        }
    } else {
        if (interrupt_raw_is_light(raw)) {
            g_light_count++;
        } else {
            g_light_count = 0;
        }

        if (g_light_count >= g_stable_samples) {
            g_sensor_interrupt = 0;
            g_dark_count = 0;
            g_light_count = 0;
            g_last_sensor_event_tick = (uint32_t)xTaskGetTickCount();

            serial_protocol_send_line("SENSOR:0");
            interrupt_apply_state();
        }
    }
}

void interrupt_control_poll(void)
{
    interrupt_control_tick();
}

int interrupt_control_is_active(void)
{
    return g_interrupt_active;
}

void interrupt_control_manual_on(void)
{
    g_manual_interrupt = 1;
    interrupt_apply_state();
}

void interrupt_control_manual_off(void)
{
    g_manual_interrupt = 0;
    g_sensor_interrupt = 0;
    g_dark_count = 0;
    g_light_count = 0;
    g_last_sensor_event_tick = (uint32_t)xTaskGetTickCount();
    interrupt_apply_state();
}

void interrupt_control_manual_toggle(void)
{
    if (g_interrupt_active) {
        interrupt_control_manual_off();
    } else {
        interrupt_control_manual_on();
    }
}

void interrupt_control_sensor_enable(int enabled)
{
    g_sensor_enabled = enabled ? 1 : 0;

    if (!g_sensor_enabled) {
        g_sensor_interrupt = 0;
        g_dark_count = 0;
        g_light_count = 0;
        interrupt_apply_state();
    }
}

void interrupt_control_sensor_rearm(void)
{
    g_sensor_interrupt = 0;
    g_dark_count = 0;
    g_light_count = 0;
    g_last_sensor_event_tick = (uint32_t)xTaskGetTickCount();
    interrupt_apply_state();
}

void interrupt_control_format_status(char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) {
        return;
    }

    interrupt_photo_read_raw();

    snprintf(buffer,
             buffer_size,
             "INTERRUPT ACTIVE=%d MANUAL=%d SENSOR=%d SENSOR_ENABLED=%d RAW=%d READ_OK=%d ADC_READY=%d ADC_UNIT=%d ADC_CHANNEL=%d PHOTO_ADC_GPIO=%d DARK_TH=%d LIGHT_TH=%d DARK_IS_LOW=%d STABLE=%d COOLDOWN_MS=%d",
             g_interrupt_active,
             g_manual_interrupt,
             g_sensor_interrupt,
             g_sensor_enabled,
             g_last_raw,
             g_last_read_ok,
             g_adc_ready,
             (int)g_adc_unit,
             (int)g_adc_channel,
             g_photo_gpio,
             g_dark_threshold,
             g_light_threshold,
             g_dark_is_low,
             g_stable_samples,
             INTERRUPT_SENSOR_COOLDOWN_MS);
}

/* ============================================================
 * Comandos extendidos.
 *
 * Si serial_protocol.c ya maneja INTERRUPT ON/OFF/TOGGLE/STATUS
 * llamando a las funciones públicas anteriores, esto no estorba.
 * Si serial_protocol.c delega a interrupt_control_handle_command(),
 * también queda soportado.
 * ============================================================ */

int interrupt_control_handle_command(const char *cmd)
{
    if (!cmd) {
        return 0;
    }

    if (strcmp(cmd, "INTERRUPT ON") == 0) {
        interrupt_control_manual_on();
        return 1;
    }

    if (strcmp(cmd, "INTERRUPT OFF") == 0) {
        interrupt_control_manual_off();
        return 1;
    }

    if (strcmp(cmd, "INTERRUPT TOGGLE") == 0) {
        interrupt_control_manual_toggle();
        return 1;
    }

    if (strcmp(cmd, "INTERRUPT STATUS") == 0) {
        char line[256];
        interrupt_control_format_status(line, sizeof(line));
        serial_protocol_send_line(line);
        return 1;
    }

    if (strcmp(cmd, "INTERRUPT RAW") == 0) {
        int raw = interrupt_photo_read_raw();

        char line[128];
        snprintf(line,
                 sizeof(line),
                 "INTERRUPT RAW=%d READ_OK=%d GPIO=%d UNIT=%d CHANNEL=%d",
                 raw,
                 g_last_read_ok,
                 g_photo_gpio,
                 (int)g_adc_unit,
                 (int)g_adc_channel);
        serial_protocol_send_line(line);
        return 1;
    }

    if (strcmp(cmd, "INTERRUPT SENSOR ON") == 0) {
        interrupt_control_sensor_enable(1);
        serial_protocol_send_line("OK INTERRUPT SENSOR ON");
        return 1;
    }

    if (strcmp(cmd, "INTERRUPT SENSOR OFF") == 0) {
        interrupt_control_sensor_enable(0);
        serial_protocol_send_line("OK INTERRUPT SENSOR OFF");
        return 1;
    }

    if (strcmp(cmd, "INTERRUPT SENSOR REARM") == 0) {
        interrupt_control_sensor_rearm();
        serial_protocol_send_line("OK INTERRUPT SENSOR REARM");
        return 1;
    }

    int value = 0;

    if (sscanf(cmd, "INTERRUPT GPIO=%d", &value) == 1) {
        if (value < 0 || value > 30) {
            serial_protocol_send_line("ERR INTERRUPT INVALID_GPIO_RANGE");
            return 1;
        }

        if (interrupt_adc_configure_gpio(value)) {
            interrupt_control_sensor_rearm();
        }

        return 1;
    }

    int dark = 0;
    int light = 0;

    if (sscanf(cmd, "INTERRUPT THRESHOLD DARK=%d LIGHT=%d", &dark, &light) == 2) {
        if (dark < 0 || light < 0) {
            serial_protocol_send_line("ERR INTERRUPT INVALID_THRESHOLD");
            return 1;
        }

        g_dark_threshold = dark;
        g_light_threshold = light;
        serial_protocol_send_line("OK INTERRUPT THRESHOLD");
        return 1;
    }

    if (sscanf(cmd, "INTERRUPT DARK_IS_LOW=%d", &value) == 1) {
        g_dark_is_low = value ? 1 : 0;
        serial_protocol_send_line("OK INTERRUPT DARK_IS_LOW");
        return 1;
    }

    if (sscanf(cmd, "INTERRUPT STABLE=%d", &value) == 1) {
        if (value <= 0) {
            serial_protocol_send_line("ERR INTERRUPT INVALID_STABLE");
            return 1;
        }

        g_stable_samples = value;
        serial_protocol_send_line("OK INTERRUPT STABLE");
        return 1;
    }

    return 0;
}
