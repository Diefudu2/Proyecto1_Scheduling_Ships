# Documentación interna del código

Este documento resume la responsabilidad de cada archivo y sus funciones principales. La documentación en comentarios también fue agregada al encabezado de cada archivo fuente.

## `canal.c`

Implementa el canal lógico, control de entrada/salida, movimiento, interrupciones, restauración post-interrupción y políticas de flujo TICO/LETRERO/EQUIDAD.

Funciones / clases principales detectadas:
- `canal_valid_position`
- `canal_ship_has_finished`
- `canal_finish_ship`
- `canal_advance_one_position`
- `canal_move_ship`
- `ship_dir_to_canal_dir`
- `canal_dir_to_ship_dir`
- `canal_opposite_dir`
- `canal_entry_position_for_ship`
- `canal_next_position_for_ship`
- `canal_visual_slot_has_other_ship`
- `canal_has_waiting_in_dir`
- `canal_flow_allows_entry`
- `canal_update_flow_when_empty`
- `canal_preemptive_ship_beats`
- `canal_try_preempt_blocker_for_ship`
- `canal_valid_position`
- `ship_dir_to_canal_dir`
- `canal_dir_to_ship_dir`
- `canal_opposite_dir`
- `canal_entry_position_for_ship`
- `canal_next_position_for_ship`
- `canal_ship_has_finished`
- `canal_position_to_led_slot`
- `canal_position_to_led_index`
- `canal_visual_slot_has_other_ship`
- `canal_has_waiting_in_dir`
- `canal_reset_flow_state`
- `canal_update_flow_when_empty`
- `canal_flow_allows_entry`
- `canal_init`
- `canal_apply_config`
- `canal_try_enter`
- `canal_finish_ship`
- `canal_interrupt_activate`
- `canal_interrupt_deactivate`
- `canal_preempt_ship`
- `canal_has_crossing_ships`
- `canal_preemptive_ship_beats`
- `canal_try_preempt_blocker_for_ship`

## `config.c`

Inicializa, valida y actualiza la configuración viva del sistema recibida por serial o por valores por defecto.

Funciones / clases principales detectadas:
- `config_min_int`
- `config_init_defaults`
- `config_set_max_queue_per_side`
- `config_set_quantum_ms`
- `config_set_letrero_ms`
- `config_set_equidad_w`
- `config_set_canal_move_interval_ms`
- `config_set_queue_visible`
- `config_set_canal_length`
- `config_set_max_ships_in_canal`
- `config_set_sched_algo`
- `config_set_flow_algo`

## `flow_control.c`

Reserva de módulo para futura separación de políticas de flujo; actualmente la fuente de verdad está en canal.c.

Funciones / clases principales detectadas:
- `flow_control_init`

## `interrupt_control.c`

Gestiona interrupciones por fotoresistencia y por GUI/serial, aplicando histéresis, cooldown y activación del cierre del canal.

Funciones / clases principales detectadas:
- `interrupt_adc_delete_unit`
- `interrupt_adc_configure_gpio`
- `interrupt_photo_read_raw`
- `interrupt_raw_is_dark`
- `interrupt_raw_is_light`
- `interrupt_sensor_cooldown_elapsed`
- `interrupt_apply_state`
- `interrupt_control_init`
- `interrupt_control_reset`
- `interrupt_control_tick`
- `interrupt_control_poll`
- `interrupt_control_is_active`
- `interrupt_control_manual_on`
- `interrupt_control_manual_off`
- `interrupt_control_manual_toggle`
- `interrupt_control_sensor_enable`
- `interrupt_control_sensor_rearm`
- `interrupt_control_format_status`
- `interrupt_control_handle_command`

## `led_view.c`

Renderiza la tira de LEDs: colas, canal comprimido, barreras, estado de interrupción y dirección de flujo.

Funciones / clases principales detectadas:
- `led_view_color_for_ship`
- `led_view_init`
- `led_view_clear`
- `led_view_test_pattern`
- `led_view_render_phase2`
- `led_view_render_phase4`

## `main.c`

Contiene la tarea principal FreeRTOS y coordina inicialización, polling serial, interrupciones, scheduler, canal y LEDs.

Funciones / clases principales detectadas:
- `task_project_core`
- `app_main`

## `scenario.c`

Carga escenarios iniciales o comandos de barcos desde cadenas de tokens n/f/p/N/F/P.

Funciones / clases principales detectadas:
- `scenario_add_token`
- `scenario_load_sequence`

## `scheduler.c`

Implementa la cola READY y los algoritmos FCFS, RR, PRIORITY, SJF, STRN y EDF.

Funciones / clases principales detectadas:
- `ready_queue_contains`
- `scheduler_thread_is_better`
- `scheduler_remove_from_ready`
- `scheduler_should_preempt`
- `scheduler_rebuild_ready_queue`
- `scheduler_has_ready_ship_dir`
- `ready_queue_contains`
- `scheduler_thread_is_better`
- `scheduler_remove_from_ready`
- `scheduler_should_preempt`
- `scheduler_init`
- `scheduler_set_algorithm`
- `scheduler_get_algorithm`
- `scheduler_start`
- `scheduler_pause`
- `scheduler_is_enabled`
- `scheduler_add_ready`
- `scheduler_rebuild_ready_queue`
- `scheduler_dispatch_to_canal`
- `scheduler_step_once`
- `scheduler_apply_preemption`
- `scheduler_note_preemption`
- `scheduler_tick`
- `scheduler_ready_count`
- `scheduler_parse_algo`
- `scheduler_print_ready_queue`
- `scheduler_print_status`

## `semaphore.c`

Implementa semáforos simulados para proteger cupos del canal y posiciones lógicas.

Funciones / clases principales detectadas:
- `sim_sem_init`
- `sim_sem_block_thread`
- `sim_sem_wait`
- `sim_sem_value`

## `serial_protocol.c`

Procesa comandos seriales de configuración, control, estado, interrupciones y snapshots para la GUI.

Funciones / clases principales detectadas:
- `serial_protocol_handle_command`
- `serial_protocol_handle_ship_command`
- `serial_protocol_handle_config_command`
- `serial_protocol_send_threads`
- `serial_protocol_send_canal`
- `serial_protocol_send_config`
- `serial_protocol_send_snapshot`
- `append_text`
- `serial_protocol_parse_short_ship`
- `serial_protocol_parse_flow`
- `serial_protocol_reset_system`
- `serial_protocol_send_line`
- `serial_protocol_init`
- `serial_protocol_parse_short_ship`
- `serial_protocol_parse_flow`
- `serial_protocol_handle_ship_command`
- `serial_protocol_send_threads`
- `serial_protocol_send_canal`
- `serial_protocol_send_config`
- `serial_protocol_handle_config_command`
- `serial_protocol_reset_system`
- `serial_protocol_handle_command`
- `append_text`
- `serial_protocol_send_snapshot`
- `serial_protocol_poll`

## `ships.c`

Gestiona creación, atributos por tipo, estados y sincronización de barcos con SimThread.

Funciones / clases principales detectadas:
- `ships_init`
- `ship_thread_step`
- `ships_count_by_dir`
- `ships_count_ready_by_dir`
- `ship_default_speed`
- `ship_default_priority`
- `ship_default_burst_ms`
- `ship_default_deadline_ms`
- `ships_get_count`
- `ship_parse_type`
- `ship_parse_dir`
- `ships_sync_states_from_threads`

## `thread.c`

Implementa la biblioteca mínima de hilos simulados usados por el scheduler del proyecto.

Funciones / clases principales detectadas:
- `thread_lib_init`
- `thread_set_ready`
- `thread_set_running`
- `thread_block`
- `thread_pause`
- `thread_preempt`
- `thread_exit`
- `thread_get_count`

## `canal.h`

Expone la estructura del canal, direcciones lógicas y API pública para entrada, avance, interrupción y diagnóstico del canal.

Funciones / clases principales detectadas:
- `canal_init`
- `canal_apply_config`
- `canal_try_enter`
- `canal_tick`
- `canal_preempt_ship`
- `canal_preempt_blocker_for_edf`
- `canal_preempt_blocker_for_algo`
- `canal_has_crossing_ships`
- `canal_interrupt_activate`
- `canal_interrupt_deactivate`
- `canal_get_ship_count`
- `canal_get_length`
- `canal_get_max_ships`
- `canal_get_active_dir`
- `canal_position_to_led_slot`
- `canal_position_to_led_index`
- `canal_set_interrupted`
- `canal_is_interrupted`
- `canal_format_flow_status`
- `canal_format_status`

## `config.h`

Define límites de compilación, valores por defecto y la estructura SystemConfig compartida por todos los módulos.

Funciones / clases principales detectadas:
- `config_init_defaults`
- `config_set_quantum_ms`
- `config_set_letrero_ms`
- `config_set_equidad_w`
- `config_set_queue_visible`
- `config_set_max_queue_per_side`
- `config_set_canal_length`
- `config_set_max_ships_in_canal`
- `config_set_sched_algo`
- `config_set_flow_algo`
- `config_set_canal_move_interval_ms`

## `interrupt_control.h`

Declara la API pública del controlador de interrupciones usado por main.c y serial_protocol.c.

Funciones / clases principales detectadas:
- `interrupt_control_init`
- `interrupt_control_reset`
- `interrupt_control_tick`
- `interrupt_control_poll`
- `interrupt_control_is_active`
- `interrupt_control_manual_on`
- `interrupt_control_manual_off`
- `interrupt_control_manual_toggle`
- `interrupt_control_sensor_enable`
- `interrupt_control_sensor_rearm`
- `interrupt_control_format_status`
- `interrupt_control_handle_command`

## `led_view.h`

Expone funciones de inicialización, limpieza, prueba y renderizado de la vista LED.

Funciones / clases principales detectadas:
- `led_view_init`
- `led_view_clear`
- `led_view_test_pattern`
- `led_view_render_phase2`
- `led_view_render_phase4`

## `scenario.h`

Declara la función de carga de escenarios.

Funciones / clases principales detectadas:
- `scenario_load_sequence`

## `scheduler.h`

Define la estructura Scheduler y la API para añadir, elegir, despachar y consultar hilos simulados.

Funciones / clases principales detectadas:
- `scheduler_init`
- `scheduler_dispatch_to_canal`
- `scheduler_set_algorithm`
- `scheduler_get_algorithm`
- `scheduler_start`
- `scheduler_pause`
- `scheduler_is_enabled`
- `scheduler_add_ready`
- `scheduler_tick`
- `scheduler_step_once`
- `scheduler_apply_preemption`
- `scheduler_note_preemption`
- `scheduler_ready_count`
- `scheduler_parse_algo`
- `scheduler_print_ready_queue`
- `scheduler_print_status`
- `scheduler_has_ready_ship_dir`

## `semaphore.h`

Define SimSemaphore y las operaciones wait/signal/value.

Funciones / clases principales detectadas:
- `sim_sem_init`
- `sim_sem_wait`
- `sim_sem_value`

## `serial_protocol.h`

Declara la inicialización, polling y escritura de líneas del protocolo serial.

Funciones / clases principales detectadas:
- `serial_protocol_init`
- `serial_protocol_poll`
- `serial_protocol_send_line`

## `ships.h`

Define tipos, estados y estructura Ship.

Funciones / clases principales detectadas:
- `ships_init`
- `ship_thread_step`
- `ship_default_speed`
- `ship_default_priority`
- `ship_default_burst_ms`
- `ship_default_deadline_ms`
- `ships_count_by_dir`
- `ships_count_ready_by_dir`
- `ships_sync_states_from_threads`
- `ships_get_count`
- `ship_parse_type`
- `ship_parse_dir`

## `thread.h`

Define SimThread, estados de hilo y API de la biblioteca de hilos simulados.

Funciones / clases principales detectadas:
- `thread_lib_init`
- `thread_set_ready`
- `thread_set_running`
- `thread_block`
- `thread_pause`
- `thread_preempt`
- `thread_exit`
- `thread_get_count`

## `gui.py`

Interfaz gráfica Tkinter de un solo hilo para controlar el firmware, visualizar canal/colas e interrupciones.

Funciones / clases principales detectadas:
- `SchedulingShipsGUI`
- `__init__`
- `_build_ui`
- `_build_connection_panel`
- `_build_runtime_panel`
- `_build_interrupt_panel`
- `_build_config_panel`
- `_build_visual_panel`
- `_build_console_panel`
- `_build_commands_panel`
- `_build_scenario_panel`
- `_tick`
- `_read_serial_available`
- `_send_auto_poll_command`
- `_update_blink`
- `refresh_ports`
- `toggle_connection`
- `send_command`
- `request_refresh`
- `send_manual`
- `apply_config`
- `pick_scenario`
- `preview_scenario`
- `parse_scenario_text`
- `load_scenario`
- `create_example_scenarios`
- `parse_line`
- `parse_key_values`
- `_parse_interrupt_status`
- `_parse_snapshot`
- `_parse_ready_snapshot`
- `_sync_config_fields`
- `_parse_canal_position`
- `_parse_ship_line`
- `draw_visual`
- `draw_flow_arrow`
- `draw_ready_queue`
- `draw_legend`
- `flow_arrow_text`
- `color_for_type`

## `serial_client.py`

Cliente serial no bloqueante usado por la GUI para comunicarse con el ESP32-C6.

Funciones / clases principales detectadas:
- `SerialClient`
- `__init__`
- `list_ports`
- `is_connected`
- `connect`
- `disconnect`
- `send`
- `poll_lines`

## Orden lógico de ejecución

1. `main.c` inicializa configuración, hilos simulados, scheduler, barcos, canal, interrupciones, LEDs y serial.
2. La GUI o consola envía comandos a `serial_protocol.c`.
3. `serial_protocol.c` actualiza `config.c`, crea barcos en `ships.c` o controla ejecución.
4. En cada ciclo, `interrupt_control.c` revisa sensor/comandos de interrupción.
5. Si no hay interrupción, `scheduler.c` intenta despachar barcos hacia `canal.c`.
6. `canal.c` valida flujo, dirección, cupos, posiciones y movimiento.
7. `led_view.c` renderiza colas, canal, barreras e indicador de flujo.

## Reglas de seguridad mantenidas

- No ocupar posiciones fuera de rango.
- No ingresar al canal si está interrumpido.
- No permitir sentidos opuestos simultáneos.
- No permitir doble ocupación lógica.
- No contar restauraciones post-interrupción como barcos nuevos de Equidad.
- No cambiar LETRERO/EQUIDAD si no hay demanda del lado contrario.