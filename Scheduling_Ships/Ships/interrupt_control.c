#include "interrupt_control.h"

#include "config.h"
#include "canal.h"
#include "serial_protocol.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"

#include <stdio.h>

/*
 * interrupt_control.c
 *
 * Interrupción del canal por fotoresistencia y control manual desde GUI.
 *
 * Diseño de estado:
 * - manual_latch: interrupción solicitada desde la GUI/serial.
 * - sensor_latch: interrupción solicitada por fotoresistencia.
 * - sensor_ack_until_light: bloqueo de rearme. Se usa cuando se libera la
 *   interrupción manualmente mientras la fotoresistencia sigue tapada, o si
 *   el sistema arranca con el sensor ya oscuro. Evita disparos continuos.
 * - sensor_seen_light: el sensor solo puede disparar después de haber visto
 *   una condición clara de luz. Esto evita que un ADC desconectado, mal
 *   umbralizado o cubierto al arrancar deje el sistema bloqueado para siempre.
 *
 * Reglas:
 * - Oscuridad estable: activa una sola vez.
 * - Luz estable: libera sensor_latch y rearma el sensor.
 * - Comando INTERRUPT OFF: libera manual/sensor y no permite re-disparo hasta
 *   que vuelva la luz si el sensor continúa oscuro.
 */

#define INTERRUPT_PHOTO_ADC_UNIT    ADC_UNIT_1
#define INTERRUPT_PHOTO_ADC_CHANNEL ADC_CHANNEL_1
#define INTERRUPT_PHOTO_ADC_ATTEN   ADC_ATTEN_DB_12
#define INTERRUPT_PHOTO_ADC_BITW    ADC_BITWIDTH_DEFAULT

static const char *TAG = "INTERRUPT";

static adc_oneshot_unit_handle_t g_adc_handle = NULL;
static int g_adc_ready = 0;

static int g_manual_latch = 0;
static int g_sensor_latch = 0;
static int g_sensor_ack_until_light = 0;
static int g_sensor_seen_light = 0;
static int g_sensor_enabled = INTERRUPT_PHOTO_SENSOR_ENABLED;

static int g_dark_samples = 0;
static int g_light_samples = 0;
static int g_last_raw = -1;

static int interrupt_is_requested(void)
{
    return g_manual_latch || g_sensor_latch;
}

static int photo_is_dark_raw(int raw)
{
#if INTERRUPT_PHOTO_DARK_IS_LOW
    return raw <= INTERRUPT_PHOTO_DARK_THRESHOLD;
#else
    return raw >= INTERRUPT_PHOTO_LIGHT_THRESHOLD;
#endif
}

static int photo_is_light_raw(int raw)
{
#if INTERRUPT_PHOTO_DARK_IS_LOW
    return raw >= INTERRUPT_PHOTO_LIGHT_THRESHOLD;
#else
    return raw <= INTERRUPT_PHOTO_DARK_THRESHOLD;
#endif
}

static void interrupt_apply_state(const char *reason)
{
    int requested = interrupt_is_requested();
    int active = canal_is_interrupted();

    if (requested && !active) {
        int removed = canal_interrupt_activate();
        char line[160];
        snprintf(line,
                 sizeof(line),
                 "INTERRUPT ON SOURCE=%s REMOVED=%d RAW=%d",
                 reason ? reason : "UNKNOWN",
                 removed,
                 g_last_raw);
        serial_protocol_send_line(line);
        return;
    }

    if (!requested && active) {
        canal_interrupt_deactivate();
        char line[160];
        snprintf(line,
                 sizeof(line),
                 "INTERRUPT OFF SOURCE=%s RAW=%d",
                 reason ? reason : "UNKNOWN",
                 g_last_raw);
        serial_protocol_send_line(line);
    }
}

static void interrupt_poll_photoresistor(void)
{
    if (!g_sensor_enabled || !g_adc_ready || !g_adc_handle) {
        return;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(g_adc_handle,
                                     INTERRUPT_PHOTO_ADC_CHANNEL,
                                     &raw);

    if (err != ESP_OK) {
        return;
    }

    g_last_raw = raw;

    int is_dark = photo_is_dark_raw(raw);
    int is_light = photo_is_light_raw(raw);

    if (is_dark) {
        g_dark_samples++;
        g_light_samples = 0;
    } else if (is_light) {
        g_light_samples++;
        g_dark_samples = 0;
    } else {
        /* Zona de histéresis: no cambia estado. */
        g_dark_samples = 0;
        g_light_samples = 0;
        return;
    }

    if (g_dark_samples > INTERRUPT_PHOTO_STABLE_SAMPLES) {
        g_dark_samples = INTERRUPT_PHOTO_STABLE_SAMPLES;
    }

    if (g_light_samples > INTERRUPT_PHOTO_STABLE_SAMPLES) {
        g_light_samples = INTERRUPT_PHOTO_STABLE_SAMPLES;
    }

    /*
     * Luz estable:
     * - Libera la interrupción causada por sensor.
     * - Rearma futuros eventos de oscuridad.
     * - Si había un ACK manual pendiente, también se limpia aquí.
     */
    if (g_light_samples >= INTERRUPT_PHOTO_STABLE_SAMPLES) {
        g_sensor_seen_light = 1;

        if (g_sensor_latch || g_sensor_ack_until_light) {
            g_sensor_latch = 0;
            g_sensor_ack_until_light = 0;
            interrupt_apply_state("PHOTO_LIGHT");
        }

        return;
    }

    /*
     * Oscuridad estable:
     * - Solo dispara si el sensor ya vio luz antes.
     * - Solo dispara una vez por evento de tapa.
     * - Si se liberó manualmente mientras seguía oscuro, espera luz para
     *   rearmarse y evitar bloqueo constante.
     */
    if (g_dark_samples >= INTERRUPT_PHOTO_STABLE_SAMPLES) {
        if (!g_sensor_seen_light) {
            g_sensor_ack_until_light = 1;
            return;
        }

        if (!g_sensor_latch && !g_sensor_ack_until_light) {
            g_sensor_latch = 1;
            interrupt_apply_state("PHOTO_DARK");
        }
    }
}

void interrupt_control_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = INTERRUPT_PHOTO_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    if (adc_oneshot_new_unit(&unit_cfg, &g_adc_handle) == ESP_OK) {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = INTERRUPT_PHOTO_ADC_ATTEN,
            .bitwidth = INTERRUPT_PHOTO_ADC_BITW,
        };

        if (adc_oneshot_config_channel(g_adc_handle,
                                       INTERRUPT_PHOTO_ADC_CHANNEL,
                                       &chan_cfg) == ESP_OK) {
            g_adc_ready = 1;
        }
    }

    g_manual_latch = 0;
    g_sensor_latch = 0;
    g_sensor_ack_until_light = 0;
    g_sensor_seen_light = 0;
    g_sensor_enabled = INTERRUPT_PHOTO_SENSOR_ENABLED;
    g_dark_samples = 0;
    g_light_samples = 0;
    g_last_raw = -1;

    ESP_LOGI(TAG,
             "Interrupción inicializada: PHOTO_ADC_GPIO=%d ADC_CH=%d SENSOR_ENABLED=%d DARK_TH=%d LIGHT_TH=%d DARK_IS_LOW=%d",
             INTERRUPT_PHOTO_ADC_GPIO,
             INTERRUPT_PHOTO_ADC_CHANNEL,
             g_sensor_enabled,
             INTERRUPT_PHOTO_DARK_THRESHOLD,
             INTERRUPT_PHOTO_LIGHT_THRESHOLD,
             INTERRUPT_PHOTO_DARK_IS_LOW);
}

void interrupt_control_reset(void)
{
    g_manual_latch = 0;
    g_sensor_latch = 0;
    g_sensor_ack_until_light = 0;
    g_sensor_seen_light = 0;
    g_dark_samples = 0;
    g_light_samples = 0;
    canal_interrupt_deactivate();
}

void interrupt_control_poll(void)
{
    interrupt_poll_photoresistor();
}

void interrupt_control_manual_on(void)
{
    g_manual_latch = 1;
    interrupt_apply_state("GUI_ON");
}

void interrupt_control_manual_off(void)
{
    g_manual_latch = 0;
    g_sensor_latch = 0;

    /*
     * Si el sensor sigue oscuro, no se permite que reactive inmediatamente.
     * Debe volver a ver luz para armar un nuevo evento de oscuridad.
     */
    g_sensor_ack_until_light = 1;

    interrupt_apply_state("GUI_OFF");
}

void interrupt_control_manual_toggle(void)
{
    if (canal_is_interrupted() || interrupt_is_requested()) {
        interrupt_control_manual_off();
    } else {
        interrupt_control_manual_on();
    }
}

void interrupt_control_sensor_enable(int enabled)
{
    g_sensor_enabled = enabled ? 1 : 0;

    if (!g_sensor_enabled) {
        g_sensor_latch = 0;
        g_sensor_ack_until_light = 0;
        g_dark_samples = 0;
        g_light_samples = 0;
        interrupt_apply_state("PHOTO_DISABLED");
    }
}

void interrupt_control_sensor_rearm(void)
{
    g_sensor_latch = 0;
    g_sensor_ack_until_light = 0;
    g_sensor_seen_light = 0;
    g_dark_samples = 0;
    g_light_samples = 0;
    interrupt_apply_state("PHOTO_REARM");
}

int interrupt_control_is_active(void)
{
    return canal_is_interrupted();
}

void interrupt_control_format_status(char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) {
        return;
    }

    snprintf(buffer,
             buffer_size,
             "INTERRUPT ACTIVE=%d MANUAL=%d SENSOR=%d SENSOR_ENABLED=%d ACK_UNTIL_LIGHT=%d SEEN_LIGHT=%d RAW=%d DARK_TH=%d LIGHT_TH=%d DARK_IS_LOW=%d STABLE=%d PHOTO_ADC_GPIO=%d",
             canal_is_interrupted(),
             g_manual_latch,
             g_sensor_latch,
             g_sensor_enabled,
             g_sensor_ack_until_light,
             g_sensor_seen_light,
             g_last_raw,
             INTERRUPT_PHOTO_DARK_THRESHOLD,
             INTERRUPT_PHOTO_LIGHT_THRESHOLD,
             INTERRUPT_PHOTO_DARK_IS_LOW,
             INTERRUPT_PHOTO_STABLE_SAMPLES,
             INTERRUPT_PHOTO_ADC_GPIO);
}
