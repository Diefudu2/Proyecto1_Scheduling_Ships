#include "serial_protocol.h"
#include "config.h"
#include "led_view.h"
#include "ships.h"
#include "scheduler.h"
#include "thread.h"
#include "canal.h"
#include "scenario.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

static const char *TAG = "SERIAL";

static char rx_line[SERIAL_LINE_SIZE];
static int rx_index = 0;

static void serial_protocol_handle_command(const char *cmd_const);
static void serial_protocol_handle_ship_command(char *cmd);
static void serial_protocol_handle_config_command(char *cmd);
static void serial_protocol_send_threads(void);
static void serial_protocol_send_canal(void);
static void serial_protocol_send_config(void);
static int serial_protocol_parse_short_ship(const char *cmd, ShipType *type, ShipDir *dir);
static int serial_protocol_parse_flow(const char *text, FlowAlgo *out);
static void serial_protocol_reset_system(void);

void serial_protocol_send_line(const char *line)
{
    if (!line) {
        return;
    }

    usb_serial_jtag_write_bytes((const uint8_t *)line,
                                strlen(line),
                                pdMS_TO_TICKS(20));
    usb_serial_jtag_write_bytes((const uint8_t *)"\r\n",
                                2,
                                pdMS_TO_TICKS(20));
}

void serial_protocol_init(void)
{
    usb_serial_jtag_driver_config_t usb_serial_jtag_config = {
        .rx_buffer_size = SERIAL_RX_BUFFER_SIZE,
        .tx_buffer_size = SERIAL_TX_BUFFER_SIZE,
    };

    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_serial_jtag_config));

    ESP_LOGI(TAG, "USB Serial/JTAG inicializado");
    serial_protocol_send_line("BOOT OK - Scheduling Ships ESP32-C6");
}

static int serial_protocol_parse_short_ship(const char *cmd, ShipType *type, ShipDir *dir)
{
    if (!cmd || !type || !dir || strlen(cmd) != 1) {
        return 0;
    }

    switch (cmd[0]) {
        case 'n': *type = SHIP_NORMAL; *dir = DIR_LEFT_TO_RIGHT; return 1;
        case 'f': *type = SHIP_FISHER; *dir = DIR_LEFT_TO_RIGHT; return 1;
        case 'p': *type = SHIP_PATROL; *dir = DIR_LEFT_TO_RIGHT; return 1;
        case 'N': *type = SHIP_NORMAL; *dir = DIR_RIGHT_TO_LEFT; return 1;
        case 'F': *type = SHIP_FISHER; *dir = DIR_RIGHT_TO_LEFT; return 1;
        case 'P': *type = SHIP_PATROL; *dir = DIR_RIGHT_TO_LEFT; return 1;
        default: return 0;
    }
}

static int serial_protocol_parse_flow(const char *text, FlowAlgo *out)
{
    if (!text || !out) {
        return 0;
    }

    if (strcmp(text, "TICO") == 0) {
        *out = FLOW_TICO;
        return 1;
    }

    if (strcmp(text, "EQUIDAD") == 0) {
        *out = FLOW_EQUIDAD;
        return 1;
    }

    if (strcmp(text, "LETRERO") == 0) {
        *out = FLOW_LETRERO;
        return 1;
    }

    return 0;
}

static void serial_protocol_handle_ship_command(char *cmd)
{
    ShipType type;
    ShipDir dir;

    if (serial_protocol_parse_short_ship(cmd, &type, &dir)) {
        Ship *s = ship_create(type, dir);

        if (!s) {
            serial_protocol_send_line("ERR SHIP_CREATE_FAILED_OR_READY_QUEUE_FULL");
            return;
        }

        char response[128];
        snprintf(response,
                 sizeof(response),
                 "OK SHIP ID=%d TYPE=%s DIR=%s SPEED=%d THREAD=T%d",
                 s->id,
                 ship_type_name(s->type),
                 ship_dir_name(s->dir),
                 s->speed,
                 s->thread ? s->thread->id : -1);

        serial_protocol_send_line(response);
        ships_sync_states_from_threads();
        led_view_render_phase4();
        return;
    }

    char type_text[32];
    char dir_text[32];

    if (sscanf(cmd, "SHIP %31s %31s", type_text, dir_text) != 2) {
        serial_protocol_send_line("ERR USAGE: SHIP NORMAL|FISHER|PATROL L|R OR n/f/p/N/F/P");
        return;
    }

    if (!ship_parse_type(type_text, &type)) {
        serial_protocol_send_line("ERR INVALID_SHIP_TYPE");
        return;
    }

    if (!ship_parse_dir(dir_text, &dir)) {
        serial_protocol_send_line("ERR INVALID_SHIP_DIR");
        return;
    }

    Ship *s = ship_create(type, dir);

    if (!s) {
        serial_protocol_send_line("ERR SHIP_CREATE_FAILED_OR_READY_QUEUE_FULL");
        return;
    }

    char response[128];
    snprintf(response,
             sizeof(response),
             "OK SHIP ID=%d TYPE=%s DIR=%s SPEED=%d THREAD=T%d",
             s->id,
             ship_type_name(s->type),
             ship_dir_name(s->dir),
             s->speed,
             s->thread ? s->thread->id : -1);

    serial_protocol_send_line(response);
    ships_sync_states_from_threads();
    led_view_render_phase4();
}

static void serial_protocol_send_threads(void)
{
    Ship *ships = ships_get_all();
    int count = ships_get_count();
    char line[224];

    snprintf(line, sizeof(line), "THREADS COUNT=%d READY=%d", count, scheduler_ready_count());
    serial_protocol_send_line(line);

    for (int i = 0; i < count; i++) {
        Ship *s = &ships[i];

        snprintf(line,
                 sizeof(line),
                 "SHIP ID=%d TYPE=%s DIR=%s STATE=%s POS=%d SPEED=%d THREAD=T%d TSTATE=%s PRIO=%d BURST=%d REM=%d DEADLINE=%d",
                 s->id,
                 ship_type_name(s->type),
                 ship_dir_name(s->dir),
                 ship_state_name(s->state),
                 s->position,
                 s->speed,
                 s->thread ? s->thread->id : -1,
                 s->thread ? thread_state_name(s->thread->state) : "NONE",
                 s->priority,
                 s->burst_ms,
                 s->remaining_ms,
                 s->deadline_ms);

        serial_protocol_send_line(line);
    }
}

static void serial_protocol_send_canal(void)
{
    char line[224];
    canal_format_status(line, sizeof(line));
    serial_protocol_send_line(line);

    int len = canal_get_length();

    for (int pos = 0; pos < len; pos++) {
        Ship *s = canal_get_ship_at_position(pos);

        if (s) {
            snprintf(line,
                     sizeof(line),
                     "POS %d SHIP=%d TYPE=%s DIR=%s SPEED=%d REM=%d",
                     pos,
                     s->id,
                     ship_type_name(s->type),
                     ship_dir_name(s->dir),
                     s->speed,
                     s->remaining_ms);
        } else {
            snprintf(line, sizeof(line), "POS %d EMPTY", pos);
        }

        serial_protocol_send_line(line);
    }
}

static void serial_protocol_send_config(void)
{
    SystemConfig *cfg = config_get();
    char line[224];

    snprintf(line,
             sizeof(line),
             "CONFIG SCHED=%s FLOW=%s QUANTUM=%d LETRERO=%d W=%d CANAL_LENGTH=%d MAX_IN_CANAL=%d MOVE_MS=%d MAX_QUEUE=%d QUEUE_VISIBLE=%d",
             scheduler_algo_name(cfg->sched_algo),
             canal_flow_name(cfg->flow_algo),
             cfg->quantum_ms,
             cfg->letrero_ms,
             cfg->equidad_w,
             cfg->canal_length,
             cfg->max_ships_in_canal,
             cfg->canal_move_interval_ms,
             cfg->max_queue_per_side,
             cfg->queue_visible);

    serial_protocol_send_line(line);
}

static void serial_protocol_handle_config_command(char *cmd)
{
    int value = 0;
    char response[160];

    if (sscanf(cmd, "CONFIG QUANTUM=%d", &value) == 1) {
        if (config_set_quantum_ms(value)) {
            snprintf(response, sizeof(response), "OK CONFIG QUANTUM=%d", value);
        } else {
            snprintf(response, sizeof(response), "ERR INVALID_QUANTUM");
        }
        serial_protocol_send_line(response);
        return;
    }

    if (sscanf(cmd, "CONFIG LETRERO=%d", &value) == 1) {
        if (config_set_letrero_ms(value)) {
            snprintf(response, sizeof(response), "OK CONFIG LETRERO=%d", value);
        } else {
            snprintf(response, sizeof(response), "ERR INVALID_LETRERO");
        }
        serial_protocol_send_line(response);
        return;
    }

    if (sscanf(cmd, "CONFIG W=%d", &value) == 1) {
        if (config_set_equidad_w(value)) {
            snprintf(response, sizeof(response), "OK CONFIG W=%d", value);
        } else {
            snprintf(response, sizeof(response), "ERR INVALID_W");
        }
        serial_protocol_send_line(response);
        return;
    }

    if (sscanf(cmd, "CONFIG MOVE_MS=%d", &value) == 1) {
        if (config_set_canal_move_interval_ms(value)) {
            snprintf(response, sizeof(response), "OK CONFIG MOVE_MS=%d", value);
        } else {
            snprintf(response, sizeof(response), "ERR INVALID_MOVE_MS");
        }
        serial_protocol_send_line(response);
        return;
    }

    if (sscanf(cmd, "CONFIG QUEUE_VISIBLE=%d", &value) == 1) {
        if (config_set_queue_visible(value)) {
            snprintf(response, sizeof(response), "OK CONFIG QUEUE_VISIBLE=%d", value);
            led_view_render_phase4();
        } else {
            snprintf(response, sizeof(response), "ERR INVALID_QUEUE_VISIBLE");
        }
        serial_protocol_send_line(response);
        return;
    }

    if (sscanf(cmd, "CONFIG MAX_QUEUE=%d", &value) == 1) {
        if (config_set_max_queue_per_side(value)) {
            snprintf(response, sizeof(response), "OK CONFIG MAX_QUEUE=%d", value);
        } else {
            snprintf(response, sizeof(response), "ERR INVALID_MAX_QUEUE");
        }
        serial_protocol_send_line(response);
        return;
    }

    if (sscanf(cmd, "CONFIG CANAL_LENGTH=%d", &value) == 1) {
        if (canal_get_ship_count() != 0) {
            serial_protocol_send_line("ERR CANAL_NOT_EMPTY");
            return;
        }

        if (config_set_canal_length(value) && canal_apply_config()) {
            snprintf(response, sizeof(response), "OK CONFIG CANAL_LENGTH=%d", value);
        } else {
            snprintf(response, sizeof(response), "ERR INVALID_CANAL_LENGTH");
        }
        serial_protocol_send_line(response);
        led_view_render_phase4();
        return;
    }

    if (sscanf(cmd, "CONFIG MAX_IN_CANAL=%d", &value) == 1) {
        if (canal_get_ship_count() != 0) {
            serial_protocol_send_line("ERR CANAL_NOT_EMPTY");
            return;
        }

        if (config_set_max_ships_in_canal(value) && canal_apply_config()) {
            snprintf(response, sizeof(response), "OK CONFIG MAX_IN_CANAL=%d", value);
        } else {
            snprintf(response, sizeof(response), "ERR INVALID_MAX_IN_CANAL");
        }
        serial_protocol_send_line(response);
        led_view_render_phase4();
        return;
    }

    char algo_text[32];

    if (sscanf(cmd, "CONFIG SCHED=%31s", algo_text) == 1) {
        SchedAlgo algo;

        if (scheduler_parse_algo(algo_text, &algo)) {
            scheduler_set_algorithm(algo);
            snprintf(response, sizeof(response), "OK CONFIG SCHED=%s", scheduler_algo_name(algo));
        } else {
            snprintf(response, sizeof(response), "ERR INVALID_SCHED");
        }
        serial_protocol_send_line(response);
        led_view_render_phase4();
        return;
    }

    char flow_text[32];

    if (sscanf(cmd, "CONFIG FLOW=%31s", flow_text) == 1) {
        FlowAlgo flow;

        if (canal_get_ship_count() != 0) {
            serial_protocol_send_line("ERR CANAL_NOT_EMPTY");
            return;
        }

        if (serial_protocol_parse_flow(flow_text, &flow) && config_set_flow_algo(flow) && canal_apply_config()) {
            snprintf(response, sizeof(response), "OK CONFIG FLOW=%s", canal_flow_name(flow));
        } else {
            snprintf(response, sizeof(response), "ERR INVALID_FLOW");
        }
        serial_protocol_send_line(response);
        led_view_render_phase4();
        return;
    }

    serial_protocol_send_line("ERR CONFIG USAGE");
}

static void serial_protocol_reset_system(void)
{
    thread_lib_init();
    scheduler_init();
    ships_init();
    canal_init();
    led_view_clear();
    serial_protocol_send_line("OK RESET");
}

static void serial_protocol_handle_command(const char *cmd_const)
{
    if (!cmd_const || cmd_const[0] == '\0') {
        return;
    }

    char cmd[SERIAL_LINE_SIZE];
    snprintf(cmd, sizeof(cmd), "%s", cmd_const);

    if (strcmp(cmd, "PING") == 0) {
        serial_protocol_send_line("PONG");
    }
    else if (strcmp(cmd, "STATUS") == 0) {
        SystemConfig *cfg = config_get();
        char line[256];

        snprintf(line,
                 sizeof(line),
                 "STATE PHASE=4 SHIPS=%d READY=%d CANAL=%d SCHED=%s RUN=%d FLOW=%s QUANTUM=%d LETRERO=%d W=%d LEN=%d MAX_IN=%d MOVE_MS=%d",
                 ships_get_count(),
                 scheduler_ready_count(),
                 canal_get_ship_count(),
                 scheduler_algo_name(scheduler_get_algorithm()),
                 scheduler_is_enabled(),
                 canal_flow_name(cfg->flow_algo),
                 cfg->quantum_ms,
                 cfg->letrero_ms,
                 cfg->equidad_w,
                 cfg->canal_length,
                 cfg->max_ships_in_canal,
                 cfg->canal_move_interval_ms);
        serial_protocol_send_line(line);
    }
    else if (strcmp(cmd, "LEDTEST") == 0) {
        led_view_test_pattern();
        serial_protocol_send_line("OK LEDTEST");
    }
    else if (strcmp(cmd, "CLEAR") == 0) {
        led_view_clear();
        serial_protocol_send_line("OK CLEAR");
    }
    else if (strcmp(cmd, "THREADS") == 0) {
        serial_protocol_send_threads();
    }
    else if (strcmp(cmd, "CANAL") == 0) {
        serial_protocol_send_canal();
    }
    else if (strcmp(cmd, "FLOW") == 0) {
        char line[256];
        canal_format_flow_status(line, sizeof(line));
        serial_protocol_send_line(line);
    }
    else if (strcmp(cmd, "CONFIG") == 0) {
        serial_protocol_send_config();
    }
    else if (strncmp(cmd, "CONFIG ", 7) == 0) {
        serial_protocol_handle_config_command(cmd);
    }
    else if (strncmp(cmd, "SHIP ", 5) == 0) {
        serial_protocol_handle_ship_command(cmd);
    }
    else if (serial_protocol_parse_short_ship(cmd, &(ShipType){0}, &(ShipDir){0})) {
        serial_protocol_handle_ship_command(cmd);
    }
    else if (strcmp(cmd, "START") == 0) {
        scheduler_start();
        serial_protocol_send_line("OK START");
    }
    else if (strcmp(cmd, "PAUSE") == 0) {
        scheduler_pause();
        serial_protocol_send_line("OK PAUSE");
    }
    else if (strcmp(cmd, "STEP") == 0) {
        while (scheduler_dispatch_to_canal()) {
            /* llenar cupos si el flujo lo permite */
        }
        canal_tick();
        ships_sync_states_from_threads();
        led_view_render_phase4();
        serial_protocol_send_line("OK STEP");
    }
    else if (strcmp(cmd, "SCHED") == 0) {
        char line[256];
        scheduler_print_status(line, sizeof(line));
        serial_protocol_send_line(line);
        scheduler_print_ready_queue(line, sizeof(line));
        serial_protocol_send_line(line);
    }
    else if (strcmp(cmd, "RESET") == 0) {
        serial_protocol_reset_system();
    }
    else {
        serial_protocol_send_line("ERR UNKNOWN_COMMAND");
    }
}

void serial_protocol_poll(void)
{
    uint8_t byte;

    while (usb_serial_jtag_read_bytes(&byte, 1, 0) == 1) {
        if (byte == '\n' || byte == '\r') {
            rx_line[rx_index] = '\0';

            if (rx_index > 0) {
                serial_protocol_handle_command(rx_line);
            }

            rx_index = 0;
        } else {
            if (rx_index < SERIAL_LINE_SIZE - 1) {
                rx_line[rx_index++] = (char)byte;
            } else {
                rx_index = 0;
                serial_protocol_send_line("ERR LINE_TOO_LONG");
            }
        }
    }
}
