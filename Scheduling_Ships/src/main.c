#include "ship.h"
#include "canal.h"
#include "gui.h"
#include "hardware_serial.h"
#include "hardware_leds.h"
#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>

/* =========================================================
 * main.c — Integración Canal nuevo + UThreads + Scheduler
 *          + GUI + ESP32-D
 * ========================================================= */

static Scheduler g_sched;

typedef struct {
    SchedAlgo sched_algo;
    int       quantum_ms;
} SchedulerConfig;

/* =========================================================
 * Utilidades de texto
 * ========================================================= */

static void trim_newline(char *s)
{
    if (!s) return;

    size_t n = strlen(s);

    while (n > 0 &&
           (s[n - 1] == '\n' ||
            s[n - 1] == '\r' ||
            s[n - 1] == ' '  ||
            s[n - 1] == '\t')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void uppercase_str(char *s)
{
    if (!s) return;

    for (; *s; s++) {
        *s = (char)toupper((unsigned char)*s);
    }
}

/* =========================================================
 * Cargar configuración del scheduler desde canal.cfg
 * ========================================================= */

static SchedulerConfig load_scheduler_config(const char *filepath)
{
    SchedulerConfig scfg;

    /*
     * Defaults seguros.
     */
    scfg.sched_algo = SS_FCFS;
    scfg.quantum_ms = 100;

    FILE *f = fopen(filepath, "r");

    if (!f) {
        return scfg;
    }

    char line[256];

    while (fgets(line, sizeof(line), f)) {
        char *p = line;

        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '#' || *p == '\0' || *p == '\n') {
            continue;
        }

        char *eq = strchr(p, '=');

        if (!eq) {
            continue;
        }

        *eq = '\0';

        char *key = p;
        char *val = eq + 1;

        trim_newline(key);
        trim_newline(val);

        while (*val == ' ' || *val == '\t') {
            val++;
        }

        if (strcmp(key, "sched_algo") == 0) {
            uppercase_str(val);

            int parsed = sched_algo_from_str(val);

            if (parsed >= 0) {
                scfg.sched_algo = (SchedAlgo)parsed;
            } else {
                fprintf(stderr,
                        "[WARN] sched_algo invalido '%s'. Usando FCFS.\n",
                        val);
                scfg.sched_algo = SS_FCFS;
            }
        } else if (strcmp(key, "quantum_ms") == 0) {
            int q = atoi(val);

            if (q > 0) {
                scfg.quantum_ms = q;
            } else {
                fprintf(stderr,
                        "[WARN] quantum_ms invalido '%s'. Usando 100.\n",
                        val);
                scfg.quantum_ms = 100;
            }
        }
    }

    fclose(f);

    return scfg;
}

/* =========================================================
 * Espera pequeña sin usleep()
 * ========================================================= */

static void sleep_ms(long ms)
{
    struct timespec ts;

    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    nanosleep(&ts, NULL);
}

/* =========================================================
 * Hilo de actualización del ESP32-D
 *
 * Este hilo NO representa barcos.
 * Solo es infraestructura visual de hardware.
 *
 * Cada 100 ms:
 *   1. Convierte el estado actual del canal a 21 LEDs.
 *   2. Envía la lista al ESP32-D por serial.
 * ========================================================= */

typedef struct {
    Canal *canal;
    HardwareSerial *hw;
    volatile int running;
} HardwareRenderArgs;

static void *hardware_render_thread(void *arg)
{
    HardwareRenderArgs *hw_args = (HardwareRenderArgs *)arg;

    if (!hw_args || !hw_args->canal || !hw_args->hw) {
        return NULL;
    }

    while (hw_args->running &&
           g_scheduler &&
           g_scheduler->active) {

        int leds[HW_LED_COUNT];

        hardware_leds_from_canal(hw_args->canal, leds);

        /*
         * No apagamos el programa si falla el envío.
         * hardware_serial_send_leds() ya maneja enabled/connected.
         */
        hardware_serial_send_leds(hw_args->hw, leds);

        sleep_ms(100);
    }

    /*
     * Al salir, apagar LEDs si el hardware sigue disponible.
     */
    if (hw_args->hw->enabled && hw_args->hw->connected) {
        int leds[HW_LED_COUNT];
        hardware_leds_clear(leds);
        hardware_serial_send_leds(hw_args->hw, leds);
    }

    return NULL;
}

/* =========================================================
 * Generar plantilla básica de configuración
 *
 * Se deja aquí porque el canal.h nuevo ya no declara
 * canal_save_config_template().
 * ========================================================= */

static int save_config_template(const char *filepath)
{
    FILE *f = fopen(filepath, "w");

    if (!f) {
        perror("save_config_template: fopen");
        return -1;
    }

    fprintf(f,
        "# Archivo de configuracion del canal\n"
        "# CE 4303 - Scheduling Ships\n\n"
        "# Algoritmo de flujo: TICO | LETRERO | EQUIDAD\n"
        "flow_algo=TICO\n\n"
        "# Algoritmo de calendarizacion: FCFS | RR | PRIORITY | SJF | STRN | EDF\n"
        "sched_algo=FCFS\n\n"
        "# Quantum en ms para RR\n"
        "quantum_ms=100\n\n"
        "canal_length=12\n"
        "max_ships=1\n"
        "queue_visible=4\n"
        "letrero_ms=4000\n"
        "equidad_w=3\n");

    fclose(f);
    return 0;
}

/* =========================================================
 * Cargar barcos desde archivo
 *
 * Formato esperado:
 *   N L
 *   F R
 *   P L
 *
 * Tipo:
 *   N = Normal
 *   F = Pesquero
 *   P = Patrulla
 *
 * Dirección:
 *   L = izquierda -> derecha
 *   R = derecha -> izquierda
 * ========================================================= */

static int load_ships_from_file(const char *filepath)
{
    FILE *f = fopen(filepath, "r");

    if (!f) {
        return 0;
    }

    char type_c;
    char dir_c;
    int count = 0;

    while (fscanf(f, " %c %c", &type_c, &dir_c) == 2) {
        if (type_c == '#') {
            fscanf(f, "%*[^\n]");
            continue;
        }

        ShipType t;
        ShipDir  d;

        switch (type_c) {
            case 'N':
            case 'n':
                t = SHIP_NORMAL;
                break;

            case 'F':
            case 'f':
                t = SHIP_FISHER;
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

        int priority = ship_default_priority(t);
        int burst    = ship_default_burst(t, g_canal->config.canal_length);
        int deadline = 0;

        if (t == SHIP_PATROL) {
            deadline = burst;
        }

        /*
         * Firma nueva:
         *
         * ship_create(type, dir, priority, burst_ms, deadline_ms)
         */
        if (ship_create(t, d, priority, burst, deadline)) {
            count++;
        }

        sleep_ms(20);
    }

    fclose(f);
    return count;
}

/* =========================================================
 * Hilos auxiliares del canal
 *
 * Estos pthreads no representan barcos. Son infraestructura.
 * ========================================================= */

static void start_flow_threads(Canal *c)
{
    if (!c) {
        return;
    }

    pthread_create(&c->flow_thread, NULL, canal_flow_controller, c);

    if (c->config.flow_algo == FLOW_LETRERO) {
        pthread_create(&c->letrero_thread, NULL, canal_letrero_thread, c);
    }
}

static void stop_flow_threads(Canal *c)
{
    if (!c) {
        return;
    }

    if (g_scheduler) {
        sched_shutdown(g_scheduler);
    }

    /*
     * Si los hilos del canal no salen por sí solos,
     * esto evita que el programa se quede congelado.
     */
    pthread_cancel(c->flow_thread);
    pthread_join(c->flow_thread, NULL);

    if (c->config.flow_algo == FLOW_LETRERO) {
        pthread_cancel(c->letrero_thread);
        pthread_join(c->letrero_thread, NULL);
    }
}

/* =========================================================
 * Hilo de entrada por teclado
 *
 * El main corre sched_loop(), por eso el teclado se atiende
 * en un pthread auxiliar.
 * ========================================================= */

typedef struct {
    GuiState *gui;
} InputArgs;

static void *input_thread_func(void *arg)
{
    InputArgs *ia = (InputArgs *)arg;

    while (ia->gui->running &&
           g_scheduler &&
           g_scheduler->active) {

        if (gui_handle_input(ia->gui)) {
            ia->gui->running = 0;

            if (g_scheduler) {
                sched_shutdown(g_scheduler);
            }

            break;
        }

        sleep_ms(16);
    }

    return NULL;
}

/* =========================================================
 * Main
 * ========================================================= */

int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "--gen-config") == 0) {
        if (save_config_template("config/canal.cfg") != 0) {
            fprintf(stderr, "[ERROR] No se pudo generar config/canal.cfg\n");
            return 1;
        }

        printf("Template generado en config/canal.cfg\n");
        return 0;
    }

    const char *config_path = (argc >= 2)
                              ? argv[1]
                              : "config/canal.cfg";

    /*
     * canal_load_config() nuevo devuelve Canal*.
     */
    Canal *canal = canal_load_config(config_path);

    if (!canal) {
        fprintf(stderr,
                "[ERROR] No se pudo leer o crear canal desde %s\n"
                "Ejecute: ./build/ships --gen-config\n",
                config_path);
        return 1;
    }

    g_canal = canal;

    /*
     * Inicializar scheduler desde canal.cfg.
     */
    SchedulerConfig sched_cfg = load_scheduler_config(config_path);

    sched_init(&g_sched, sched_cfg.sched_algo, sched_cfg.quantum_ms);

    fprintf(stderr,
            "[INFO] Scheduler: %s | quantum_ms=%d\n",
            sched_algo_name(sched_cfg.sched_algo),
            sched_cfg.quantum_ms);

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
        sleep_ms(2000);
    }

    GuiState gui;
    gui_init(&gui, canal);

    pthread_t render_tid;
    pthread_create(&render_tid, NULL, gui_render_thread, &gui);

    /*
     * Hilo de actualización del ESP32-D.
     * Se inicia antes de cargar barcos para que el hardware
     * refleje colas/canal desde el comienzo.
     */
    HardwareRenderArgs hw_args;
    hw_args.canal = canal;
    hw_args.hw = &hw;
    hw_args.running = 1;

    pthread_t hardware_tid;
    pthread_create(&hardware_tid, NULL, hardware_render_thread, &hw_args);

    start_flow_threads(canal);

    int loaded = 0;

    loaded += load_ships_from_file("config/ships_left.txt");
    loaded += load_ships_from_file("config/ships_right.txt");

    if (loaded > 0) {
        gui_log(&gui, "Carga definida: %d barcos cargados.", loaded);
    } else {
        gui_log(&gui, "Use n/N/f/F/p/P para generar barcos.");
    }

    /*
     * Hilo de teclado.
     */
    InputArgs input_args;
    input_args.gui = &gui;

    pthread_t input_tid;
    pthread_create(&input_tid, NULL, input_thread_func, &input_args);

    /*
     * Punto central:
     * aquí corren los barcos como UThreads.
     */
    sched_loop(&g_sched);

    /*
     * Si sched_loop() terminó, apagamos GUI y hardware.
     */
    gui.running = 0;
    hw_args.running = 0;

    pthread_join(input_tid, NULL);
    pthread_join(render_tid, NULL);
    pthread_join(hardware_tid, NULL);

    /*
     * Cerrar puerto serial del ESP32-D.
     */
    hardware_serial_close(&hw);

    /*
     * Importante:
     * Restaurar la terminal antes de esperar hilos auxiliares
     * que podrían tardar en cerrar.
     */
    gui_destroy(&gui);

    stop_flow_threads(canal);

    printf("\n=== Resumen final ===\n");
    printf("Total cruzaron : %ld\n", canal->state.total_crossed);
    printf("  L->R         : %ld\n", canal->state.total_left);
    printf("  R->L         : %ld\n", canal->state.total_right);
    printf("====================\n");

    canal_destroy(canal);
    g_canal = NULL;

    return 0;
}