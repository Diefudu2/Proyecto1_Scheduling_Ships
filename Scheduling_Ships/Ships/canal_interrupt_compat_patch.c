/*
 * canal_interrupt_compat_patch.c
 *
 * COPIAR estas funciones al final de Ships/canal.c,
 * preferiblemente después de canal_is_interrupted().
 *
 * Motivo:
 * interrupt_control.c llama canal_interrupt_activate() y
 * canal_interrupt_deactivate(), pero canal.c no las estaba definiendo.
 */

int canal_interrupt_activate(void)
{
    int removed = 0;

    /*
     * Marcar interrupción antes de evacuar.
     * Mientras interrupted=1, canal_tick() no mueve barcos y
     * canal_try_enter() debe rechazar nuevas entradas.
     */
    g_canal.interrupted = 1;

    /*
     * Sacar todos los barcos del canal y devolverlos a READY.
     * No se llama canal_update_flow_when_empty() aquí porque una
     * interrupción NO es un vaciado normal del flujo.
     *
     * Importante:
     * - Guarda saved_position.
     * - Conserva remaining_ms.
     * - Libera semáforos de posición y cupo.
     * - No marca DONE.
     */
    for (int pos = 0; pos < g_canal.length; pos++) {
        Ship *ship = g_canal.positions[pos];

        if (!ship || !ship->thread) {
            continue;
        }

        ship->saved_position = pos;
        ship->thread->saved_position = pos;

        g_canal.positions[pos] = NULL;
        sim_sem_signal(&g_canal.sem_positions[pos]);
        sim_sem_signal(&g_canal.sem_cpu_slots);

        if (g_canal.ship_count > 0) {
            g_canal.ship_count--;
        }

        ship->position = -1;
        ship->state = SHIP_READY;

        thread_preempt(ship->thread);
        scheduler_add_ready(ship->thread);

        removed++;
    }

    /*
     * No forzar active_dir=FREE.
     * Se conserva la dirección activa para que, al quitar la interrupción,
     * los barcos evacuados intenten restaurar en el mismo sentido.
     */

    return removed;
}

void canal_interrupt_deactivate(void)
{
    /*
     * Quitar interrupción. El flujo/scheduler vuelve a decidir entradas.
     * La dirección activa conservada ayuda a que los barcos evacuados del
     * sentido original restauren antes de cambiar de lado.
     */
    g_canal.interrupted = 0;
}
