#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "config.h"
#include "serial_protocol.h"
#include "led_view.h"
#include "thread.h"
#include "scheduler.h"
#include "ships.h"
#include "canal.h"
#include "scenario.h"

static const char *TAG = "MAIN";

static void task_project_core(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Inicializando Scheduling Ships Fase 4");

    config_init_defaults();

    thread_lib_init();
    scheduler_init();
    ships_init();
    canal_init();

    led_view_init();
    serial_protocol_init();
    led_view_clear();

    SystemConfig *cfg = config_get();

    if (cfg->boot_scenario_enabled &&
        cfg->boot_scenario_sequence &&
        cfg->boot_scenario_sequence[0] != '\0') {
        serial_protocol_send_line("BOOT SCENARIO DETECTED");
        scenario_load_sequence(cfg->boot_scenario_sequence);
    } else {
        serial_protocol_send_line("BOOT SCENARIO EMPTY - STARTING BLANK");
    }

    serial_protocol_send_line("COMMANDS:");
    serial_protocol_send_line("  START | PAUSE | STEP | RESET");
    serial_protocol_send_line("  STATUS | CONFIG | THREADS | SCHED | CANAL | FLOW");
    serial_protocol_send_line("  CLEAR | LEDTEST");
    serial_protocol_send_line("  n f p N F P");
    serial_protocol_send_line("  SHIP NORMAL|FISHER|PATROL L|R");
    serial_protocol_send_line("  CONFIG SCHED=FCFS|RR|PRIORITY|SJF|STRN|EDF");
    serial_protocol_send_line("  CONFIG FLOW=TICO|EQUIDAD|LETRERO");
    serial_protocol_send_line("  CONFIG QUANTUM=<ms>");
    serial_protocol_send_line("  CONFIG LETRERO=<ms>");
    serial_protocol_send_line("  CONFIG W=<num>");
    serial_protocol_send_line("  CONFIG CANAL_LENGTH=<1..100>");
    serial_protocol_send_line("  CONFIG MAX_IN_CANAL=<num>");
    serial_protocol_send_line("  CONFIG MOVE_MS=<ms>");
    serial_protocol_send_line("  CONFIG MAX_QUEUE=<num>");
    serial_protocol_send_line("  CONFIG QUEUE_VISIBLE=<1..4>");

    while (1) {
        cfg = config_get();

        serial_protocol_poll();

        if (scheduler_is_enabled()) {
            /*
            * Primero aplicar apropiación.
            * RR, STRN y EDF pueden sacar un barco del canal
            * y devolverlo a READY sin destruirlo.
            */
            scheduler_apply_preemption();

            /*
             * Primero se mueve el canal. Si un barco apropiativo alcanza
             * a un bloqueador durante este avance, canal.c puede quitar
             * al bloqueador y permitir que el ganador tome el recurso
             * inmediatamente. Después de mover se rellenan cupos libres.
             */
            canal_tick();

            while (scheduler_dispatch_to_canal()) {
                /* llenar cupos disponibles mientras el flujo lo permita */
            }

            ships_sync_states_from_threads();
            led_view_render_phase4();
        }

        vTaskDelay(pdMS_TO_TICKS(cfg->system_tick_ms));
    }
}

void app_main(void)
{
    xTaskCreate(
        task_project_core,
        "project_core",
        8192,
        NULL,
        5,
        NULL
    );
}
