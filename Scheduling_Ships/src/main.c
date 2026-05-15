#include "ship.h"
#include "canal.h"
#include "gui.h"
#include "hardware_serial.h"
#include "hardware_leds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

Canal *g_canal = NULL;

typedef struct {
    Canal *canal;
    HardwareSerial *hw;
    int running;
} HardwareRenderArgs;

static void *hardware_render_thread(void *arg)
{
    HardwareRenderArgs *hw_args = (HardwareRenderArgs *)arg;

    if (!hw_args || !hw_args->canal || !hw_args->hw) {
        return NULL;
    }

    while (hw_args->running && hw_args->canal->running) {
        int leds[HW_LED_COUNT];

        hardware_leds_from_canal(hw_args->canal, leds);

        /*
         * No marcamos connected = 0 si falla un envio.
         * Si se marca como desconectado, un fallo temporal
         * puede apagar la comunicacion para siempre.
         */
        hardware_serial_send_leds(hw_args->hw, leds);

        usleep(100000); /* 100 ms */
    }

    return NULL;
}

static int load_ships_from_file(const char *filepath, int canal_length)
{
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;

    char type_c, dir_c;
    int  count = 0;

    while (fscanf(f, " %c %c", &type_c, &dir_c) == 2) {
        if (type_c == '#') {
            fscanf(f, "%*[^\n]");
            continue;
        }

        ShipType  t;
        Direction d;

        switch (type_c) {
            case 'N':
            case 'n':
                t = SHIP_NORMAL;
                break;

            case 'F':
            case 'f':
                t = SHIP_FISHING;
                break;

            case 'P':
            case 'p':
                t = SHIP_PATROL;
                break;

            default:
                continue;
        }

        switch (dir_c) {
            case 'L':
            case 'l':
                d = DIR_LEFT;
                break;

            case 'R':
            case 'r':
                d = DIR_RIGHT;
                break;

            default:
                continue;
        }

        int deadline = (t == SHIP_PATROL)
                       ? ship_default_burst(SHIP_PATROL, canal_length)
                       : 0;

        if (ship_create(t, d, canal_length, deadline)) {
            count++;
        }

        usleep(20000);
    }

    fclose(f);
    return count;
}

static pthread_t g_flow_thread;
static pthread_t g_letrero_thread;

static void start_flow_threads(Canal *c)
{
    pthread_create(&g_flow_thread, NULL, canal_flow_controller, c);

    if (c->cfg.flow_algo == FLOW_LETRERO) {
        pthread_create(&g_letrero_thread, NULL, canal_letrero_thread, c);
    }
}

static void stop_flow_threads(Canal *c)
{
    c->running = 0;
    pthread_cond_broadcast(&c->can_enter);

    pthread_join(g_flow_thread, NULL);

    if (c->cfg.flow_algo == FLOW_LETRERO) {
        pthread_join(g_letrero_thread, NULL);
    }
}

int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "--gen-config") == 0) {
        canal_save_config_template("config/canal.cfg");
        printf("Template generado en config/canal.cfg\n");
        return 0;
    }

    const char *config_path = (argc >= 2) ? argv[1] : "config/canal.cfg";

    CanalConfig cfg;
    if (canal_load_config(&cfg, config_path) != 0) {
        fprintf(stderr,
                "[ERROR] No se pudo leer %s\n"
                "Ejecute: ./ships --gen-config\n",
                config_path);
        return 1;
    }

    if (cfg.max_queue > 4) {
        cfg.max_queue = 4;
    }

    Canal canal;
    canal_init(&canal, &cfg);
    g_canal = &canal;

    /*
     * Hardware ESP32-D.
     * Por ahora queda fijo en /dev/ttyUSB0 porque el test ya funciono ahi.
     */
    HardwareSerial hw;
    int hardware_enabled = 1;

    if (hardware_serial_init(&hw, "/dev/ttyUSB0", 115200, hardware_enabled) != 0) {
        fprintf(stderr,
                "[WARN] No se pudo conectar al ESP32-D en /dev/ttyUSB0: %s\n",
                hw.last_error);
        fprintf(stderr,
                "[WARN] El programa continuara solo con GUI.\n");

        hw.enabled = 0;
        hw.connected = 0;
    }

    /*
     * Prueba inicial de 2 segundos.
     * Si esto enciende, sabemos que el main si esta hablando con el ESP32-D.
     */
    if (hw.enabled && hw.connected) {
        int test_leds[HW_LED_COUNT] = {
            1, 2, 0, 0,
            4,
            0, 0, 3, 0, 0,
            0, 0, 0, 0, 0,
            4,
            1, 0, 2, 0,
            6
        };

        hardware_serial_send_leds(&hw, test_leds);
        usleep(2000000);
    }

    GuiState gui;
    gui_init(&gui, &canal);

    pthread_t render_tid;
    pthread_create(&render_tid, NULL, gui_render_thread, &gui);

    HardwareRenderArgs hw_args;
    hw_args.canal = &canal;
    hw_args.hw = &hw;
    hw_args.running = 1;

    pthread_t hardware_tid;
    pthread_create(&hardware_tid, NULL, hardware_render_thread, &hw_args);

    start_flow_threads(&canal);

    int loaded = 0;
    loaded += load_ships_from_file("config/ships_left.txt",  cfg.canal_length);
    loaded += load_ships_from_file("config/ships_right.txt", cfg.canal_length);

    if (loaded > 0) {
        gui_log(&gui, "Carga definida: %d barcos cargados.", loaded);
    } else {
        gui_log(&gui, "Use n/N/f/F/p/P para generar barcos.");
    }

    while (gui.running && canal.running) {
        if (gui_handle_input(&gui)) {
            break;
        }

        usleep(16000);
    }

    canal.running = 0;
    gui.running = 0;
    hw_args.running = 0;

    pthread_cond_broadcast(&canal.can_enter);

    pthread_join(render_tid, NULL);
    pthread_join(hardware_tid, NULL);

    hardware_serial_close(&hw);

    stop_flow_threads(&canal);
    gui_destroy(&gui);

    printf("\n=== Resumen final ===\n");
    printf("Total cruzaron : %d\n", canal.total_crossed);
    printf("  L->R         : %d\n", canal.total_left);
    printf("  R->L         : %d\n", canal.total_right);
    printf("====================\n");

    canal_destroy(&canal);

    return 0;
}