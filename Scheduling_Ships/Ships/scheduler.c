#include "scheduler.h"
#include "config.h"
#include "canal.h"
#include "ships.h"

#include <stdio.h>
#include <string.h>

static Scheduler g_scheduler;

/* ============================================================
 * Prototipos internos
 * ============================================================ */

static int ready_queue_contains(SimThread *t);
static int scheduler_thread_is_better(SimThread *a, SimThread *b);
static SimThread *scheduler_find_best_ready(void);
static void scheduler_remove_from_ready(SimThread *target);
static int scheduler_should_preempt(SimThread *current, SimThread *candidate);
static void scheduler_rebuild_ready_queue(void);

/* ============================================================
 * Utilidades internas
 * ============================================================ */

int scheduler_has_ready_ship_dir(ShipDir dir)
{
    SimThread *cur = g_scheduler.ready_head;

    while (cur) {
        Ship *ship = (Ship *)cur->arg;

        if (ship && ship->dir == dir && cur->state == THREAD_READY) {
            return 1;
        }

        cur = cur->next;
    }

    return 0;
}

static int ready_queue_contains(SimThread *t)
{
    SimThread *cur = g_scheduler.ready_head;

    while (cur) {
        if (cur == t) {
            return 1;
        }

        cur = cur->next;
    }

    return 0;
}

static int scheduler_thread_is_better(SimThread *a, SimThread *b)
{
    if (!a) {
        return 0;
    }

    if (!b) {
        return 1;
    }

    switch (g_scheduler.algo) {
        case SCHED_PRIORITY:
            if (a->priority != b->priority) {
                return a->priority > b->priority;
            }
            return a->arrival_tick < b->arrival_tick;

        case SCHED_SJF:
            if (a->burst_ms != b->burst_ms) {
                return a->burst_ms < b->burst_ms;
            }
            return a->arrival_tick < b->arrival_tick;

        case SCHED_STRN:
            if (a->remaining_ms != b->remaining_ms) {
                return a->remaining_ms < b->remaining_ms;
            }
            return a->arrival_tick < b->arrival_tick;

        case SCHED_EDF:
            if (a->deadline_ms != b->deadline_ms) {
                return a->deadline_ms < b->deadline_ms;
            }
            return a->arrival_tick < b->arrival_tick;

        case SCHED_FCFS:
        case SCHED_RR:
        default:
            return 0;
    }
}

static SimThread *scheduler_find_best_ready(void)
{
    SimThread *best = g_scheduler.ready_head;
    SimThread *cur = g_scheduler.ready_head;

    if (!cur) {
        return NULL;
    }

    if (g_scheduler.algo == SCHED_FCFS || g_scheduler.algo == SCHED_RR) {
        return g_scheduler.ready_head;
    }

    while (cur) {
        if (cur->state == THREAD_READY && scheduler_thread_is_better(cur, best)) {
            best = cur;
        }

        cur = cur->next;
    }

    return best;
}

static void scheduler_remove_from_ready(SimThread *target)
{
    if (!target) {
        return;
    }

    SimThread *prev = NULL;
    SimThread *cur = g_scheduler.ready_head;

    while (cur) {
        if (cur == target) {
            if (prev) {
                prev->next = cur->next;
            } else {
                g_scheduler.ready_head = cur->next;
            }

            if (g_scheduler.ready_tail == cur) {
                g_scheduler.ready_tail = prev;
            }

            cur->next = NULL;

            if (g_scheduler.ready_count > 0) {
                g_scheduler.ready_count--;
            }

            return;
        }

        prev = cur;
        cur = cur->next;
    }
}

static int scheduler_should_preempt(SimThread *current, SimThread *candidate)
{
    if (!current || !candidate) {
        return 0;
    }

    switch (g_scheduler.algo) {
        case SCHED_RR:
            return current->quantum_used_ms >= g_scheduler.quantum_ms;

        case SCHED_PRIORITY:
            return 0;

        case SCHED_STRN:
            return candidate->remaining_ms < current->remaining_ms;

        case SCHED_EDF:
            return candidate->deadline_ms < current->deadline_ms;

        case SCHED_FCFS:
        case SCHED_SJF:
        default:
            return 0;
    }
}

/* ============================================================
 * API pública
 * ============================================================ */

void scheduler_init(void)
{
    memset(&g_scheduler, 0, sizeof(g_scheduler));

    SystemConfig *cfg = config_get();

    g_scheduler.algo = cfg->sched_algo;
    g_scheduler.quantum_ms = cfg->quantum_ms;
    g_scheduler.ready_head = NULL;
    g_scheduler.ready_tail = NULL;
    g_scheduler.ready_count = 0;
    g_scheduler.running = NULL;
    g_scheduler.enabled = 0;
    g_scheduler.total_ticks = 0;
    g_scheduler.total_preemptions = 0;
    g_scheduler.total_finished = 0;
}

void scheduler_set_algorithm(SchedAlgo algo)
{
    g_scheduler.algo = algo;
    config_set_sched_algo(algo);
    scheduler_rebuild_ready_queue();
}

SchedAlgo scheduler_get_algorithm(void)
{
    return g_scheduler.algo;
}

void scheduler_start(void)
{
    g_scheduler.enabled = 1;
}

void scheduler_pause(void)
{
    g_scheduler.enabled = 0;
}

int scheduler_is_enabled(void)
{
    return g_scheduler.enabled;
}

void scheduler_add_ready(SimThread *t)
{
    if (!t || t->state == THREAD_DONE) {
        return;
    }

    if (ready_queue_contains(t)) {
        return;
    }

    t->state = THREAD_READY;
    t->next = NULL;

    if (g_scheduler.algo == SCHED_FCFS || g_scheduler.algo == SCHED_RR) {
        if (g_scheduler.ready_tail) {
            g_scheduler.ready_tail->next = t;
        } else {
            g_scheduler.ready_head = t;
        }

        g_scheduler.ready_tail = t;
        g_scheduler.ready_count++;
        return;
    }

    if (!g_scheduler.ready_head || scheduler_thread_is_better(t, g_scheduler.ready_head)) {
        t->next = g_scheduler.ready_head;
        g_scheduler.ready_head = t;

        if (!g_scheduler.ready_tail) {
            g_scheduler.ready_tail = t;
        }

        g_scheduler.ready_count++;
        return;
    }

    SimThread *prev = g_scheduler.ready_head;
    SimThread *cur = g_scheduler.ready_head->next;

    while (cur && !scheduler_thread_is_better(t, cur)) {
        prev = cur;
        cur = cur->next;
    }

    prev->next = t;
    t->next = cur;

    if (!cur) {
        g_scheduler.ready_tail = t;
    }

    g_scheduler.ready_count++;
}

static void scheduler_rebuild_ready_queue(void)
{
    SimThread *old_head = g_scheduler.ready_head;

    g_scheduler.ready_head = NULL;
    g_scheduler.ready_tail = NULL;
    g_scheduler.ready_count = 0;

    while (old_head) {
        SimThread *next = old_head->next;
        old_head->next = NULL;

        if (old_head->state == THREAD_READY) {
            scheduler_add_ready(old_head);
        }

        old_head = next;
    }
}

SimThread *scheduler_pick_next(void)
{
    SimThread *selected = scheduler_find_best_ready();

    if (!selected) {
        return NULL;
    }

    scheduler_remove_from_ready(selected);
    selected->next = NULL;
    selected->quantum_used_ms = 0;

    thread_set_running(selected);
    g_scheduler.running = selected;

    return selected;
}

int scheduler_dispatch_to_canal(void)
{
    SimThread *cur = g_scheduler.ready_head;

    while (cur) {
        SimThread *candidate = cur;
        cur = cur->next;

        if (candidate->state != THREAD_READY || !candidate->arg) {
            continue;
        }

        Ship *ship = (Ship *)candidate->arg;

        if (canal_try_enter(ship)) {
            scheduler_remove_from_ready(candidate);
            candidate->next = NULL;
            candidate->quantum_used_ms = 0;
            g_scheduler.running = candidate;
            return 1;
        }
    }

    return 0;
}

void scheduler_step_once(void)
{
    SystemConfig *cfg = config_get();
    int tick_ms = cfg->system_tick_ms;

    g_scheduler.total_ticks++;

    if (g_scheduler.running) {
        SimThread *candidate = scheduler_find_best_ready();

        if (scheduler_should_preempt(g_scheduler.running, candidate)) {
            SimThread *old = g_scheduler.running;
            thread_preempt(old);
            scheduler_add_ready(old);
            g_scheduler.running = NULL;
            g_scheduler.total_preemptions++;
        }
    }

    if (!g_scheduler.running) {
        scheduler_pick_next();
    }

    if (!g_scheduler.running) {
        return;
    }

    SimThread *current = g_scheduler.running;

    if (current->entry_step) {
        current->entry_step(current->arg);
    }

    current->remaining_ms -= tick_ms;
    current->quantum_used_ms += tick_ms;

    if (current->remaining_ms < 0) {
        current->remaining_ms = 0;
    }

    if (current->remaining_ms == 0) {
        thread_exit(current);
        g_scheduler.running = NULL;
        g_scheduler.total_finished++;
        return;
    }

    if (g_scheduler.algo == SCHED_RR && current->quantum_used_ms >= g_scheduler.quantum_ms) {
        thread_preempt(current);
        scheduler_add_ready(current);
        g_scheduler.running = NULL;
        g_scheduler.total_preemptions++;
    }
}

int scheduler_apply_preemption(void)
{
    SystemConfig *cfg = config_get();

    if (!g_scheduler.enabled) {
        return 0;
    }

    g_scheduler.quantum_ms = cfg->quantum_ms;

    /*
     * 1. Apropiación por bloqueo dentro del canal.
     *
     * Esto evidencia la característica principal:
     * el barco apropiativo puede adueñarse del recurso
     * si otro barco menos adecuado lo está bloqueando.
     */
    if (g_scheduler.algo == SCHED_RR ||
        g_scheduler.algo == SCHED_STRN ||
        g_scheduler.algo == SCHED_EDF) {

        if (canal_preempt_blocker_for_algo(g_scheduler.algo)) {
            g_scheduler.total_preemptions++;
            return 1;
        }
    }

    /*
     * Si no hay READY, no hay candidato externo para apropiación.
     */
    if (!g_scheduler.ready_head) {
        return 0;
    }

    SimThread *best_ready = scheduler_find_best_ready();

    if (!best_ready) {
        return 0;
    }

    Ship *target_ship = NULL;
    SimThread *target_thread = NULL;

    int len = canal_get_length();

    /*
     * 2. Apropiación desde READY contra barcos en canal.
     */
    for (int pos = 0; pos < len; pos++) {
        Ship *ship = canal_get_ship_at_position(pos);

        if (!ship || !ship->thread) {
            continue;
        }

        if (ship->state != SHIP_CROSSING) {
            continue;
        }

        SimThread *running = ship->thread;

        /*
         * Mientras está en canal consume quantum.
         */
        running->quantum_used_ms += cfg->system_tick_ms;

        switch (g_scheduler.algo) {
            case SCHED_RR:
                /*
                 * RR: si agotó quantum y hay alguien en READY,
                 * se apropia el barco actual.
                 */
                if (running->quantum_used_ms >= g_scheduler.quantum_ms) {
                    target_ship = ship;
                    target_thread = running;
                }
                break;

            case SCHED_STRN:
                /*
                 * STRN: READY con menor remaining_ms apropia
                 * al que tenga mayor remaining_ms dentro del canal.
                 */
                if (best_ready->remaining_ms < running->remaining_ms) {
                    if (!target_thread ||
                        running->remaining_ms > target_thread->remaining_ms) {
                        target_ship = ship;
                        target_thread = running;
                    }
                }
                break;

            case SCHED_EDF:
                /*
                 * EDF: READY con deadline más urgente apropia
                 * al menos urgente dentro del canal.
                 */
                if (best_ready->deadline_ms < running->deadline_ms) {
                    if (!target_thread ||
                        running->deadline_ms > target_thread->deadline_ms) {
                        target_ship = ship;
                        target_thread = running;
                    }
                }
                break;

            case SCHED_PRIORITY:
            case SCHED_FCFS:
            case SCHED_SJF:
            default:
                break;
        }

        /*
         * Para RR basta con apropiar uno por ciclo.
         */
        if (target_ship && g_scheduler.algo == SCHED_RR) {
            break;
        }
    }

    if (!target_ship || !target_thread) {
        return 0;
    }

    if (canal_preempt_ship(target_ship)) {
        target_thread->quantum_used_ms = 0;

        if (g_scheduler.running == target_thread) {
            g_scheduler.running = NULL;
        }

        g_scheduler.total_preemptions++;
        return 1;
    }

    return 0;
}


void scheduler_note_preemption(void)
{
    /*
     * Contador centralizado de apropiaciones.
     * canal.c lo usa cuando la apropiación ocurre dentro del avance
     * físico/lógico del canal, por ejemplo cuando EDF o STRN quitan
     * directamente el barco que bloquea al proceso más urgente.
     */
    g_scheduler.total_preemptions++;
}

void scheduler_tick(void)
{
    if (!g_scheduler.enabled) {
        return;
    }

    scheduler_step_once();
}

int scheduler_ready_count(void)
{
    return g_scheduler.ready_count;
}

SimThread *scheduler_get_running(void)
{
    return g_scheduler.running;
}

const char *scheduler_algo_name(SchedAlgo algo)
{
    switch (algo) {
        case SCHED_FCFS:     return "FCFS";
        case SCHED_RR:       return "RR";
        case SCHED_PRIORITY: return "PRIORITY";
        case SCHED_SJF:      return "SJF";
        case SCHED_STRN:     return "STRN";
        case SCHED_EDF:      return "EDF";
        default:             return "UNKNOWN";
    }
}

int scheduler_parse_algo(const char *text, SchedAlgo *out)
{
    if (!text || !out) {
        return 0;
    }

    if (strcmp(text, "FCFS") == 0) {
        *out = SCHED_FCFS;
        return 1;
    }

    if (strcmp(text, "RR") == 0) {
        *out = SCHED_RR;
        return 1;
    }

    if (strcmp(text, "PRIORITY") == 0 || strcmp(text, "PRIO") == 0) {
        *out = SCHED_PRIORITY;
        return 1;
    }

    if (strcmp(text, "SJF") == 0) {
        *out = SCHED_SJF;
        return 1;
    }

    if (strcmp(text, "STRN") == 0) {
        *out = SCHED_STRN;
        return 1;
    }

    if (strcmp(text, "EDF") == 0) {
        *out = SCHED_EDF;
        return 1;
    }

    return 0;
}

void scheduler_print_ready_queue(char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) {
        return;
    }

    int used = 0;
    SimThread *cur = g_scheduler.ready_head;

    used += snprintf(buffer + used, buffer_size - used, "READY_QUEUE ");

    if (!cur) {
        snprintf(buffer, buffer_size, "READY_QUEUE EMPTY");
        return;
    }

    while (cur && used < buffer_size) {
        used += snprintf(buffer + used,
                         buffer_size - used,
                         "[T%d REM=%d PRIO=%d DEAD=%d] ",
                         cur->id,
                         cur->remaining_ms,
                         cur->priority,
                         cur->deadline_ms);
        cur = cur->next;
    }
}

void scheduler_print_status(char *buffer, int buffer_size)
{
    if (!buffer || buffer_size <= 0) {
        return;
    }

    snprintf(buffer,
             buffer_size,
             "SCHED STATUS ALGO=%s ENABLED=%d READY=%d QUANTUM=%d TICKS=%d PREEMPTIONS=%d FINISHED=%d",
             scheduler_algo_name(g_scheduler.algo),
             g_scheduler.enabled,
             g_scheduler.ready_count,
             g_scheduler.quantum_ms,
             g_scheduler.total_ticks,
             g_scheduler.total_preemptions,
             g_scheduler.total_finished);
}

SimThread *scheduler_get_ready_head(void)
{
    return g_scheduler.ready_head;
}
