#include "canal.h"
#include "ship.h"
#include "scheduler.h"
#include "uthread.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

/* =========================================================
 * canal.c — Monitor de sincronización del canal
 *
 * RESUMEN DE CAMBIOS vs versión pthreads:
 *
 *  1. Eliminado pthread_cond_t de CanalState
 *       ANTES: pthread_cond_broadcast(&c->cond) para despertar
 *              barcos bloqueados en canal_enter()
 *       AHORA: sched_add(g_scheduler, ship->uth) por cada
 *              barco cuya condición se cumple
 *
 *  2. canal_enter() → DEPRECADA
 *       ANTES: bloqueaba con pthread_cond_wait hasta poder
 *              entrar al canal
 *       AHORA: no debe usarse; ship.c maneja la espera con
 *              canal_can_enter() + uthread_block()
 *
 *  3. canal_enter_immediate() → NUEVA
 *       Registra un barco sin bloquear, asumiendo que
 *       canal_can_enter() ya fue verificado por el llamador
 *
 *  4. canal_exit() → misma firma, diferente señalización
 *       ANTES: pthread_cond_broadcast() para todos los
 *              barcos bloqueados
 *       AHORA: recorre las colas de espera y llama
 *              sched_add() solo para los barcos que cumplen
 *              la condición de entrada en este momento
 *
 *  5. canal_interrupt() → usa sched_add() directamente
 *       Los barcos expulsados se reinsertan al scheduler
 *       sin pasar por pthread_cond_signal
 *
 *  INVARIANTE PRINCIPAL (sin cambios):
 *    Nunca hay dos barcos en la misma posición del canal.
 *    Nunca hay barcos de direcciones opuestas simultáneos.
 * ========================================================= */

/* ---------------------------------------------------------
 * Global
 * --------------------------------------------------------- */
Canal *g_canal = NULL;


/* =========================================================
 * SECCIÓN 1 — Helpers internos
 * ========================================================= */

/* Verifica si el barco puede entrar según dirección activa
 * y política de flujo. Asume mutex tomado.                 */
static int can_enter_locked(Canal *c, Ship *s)
{
    CanalState *st = &c->state;

    /* Canal bloqueado por interrupción */
    if (st->interrupted) return 0;

    /* Canal lleno */
    if (st->ship_count >= c->config.max_ships) return 0;

    /* Dirección libre: cualquier barco puede entrar */
    if (st->direction == CANAL_DIR_FREE) return 1;

    /* Verificar compatibilidad de dirección */
    CanalDir needed = (s->dir == DIR_LEFT)
                      ? CANAL_DIR_LEFT
                      : CANAL_DIR_RIGHT;
    if (st->direction != needed) return 0;

    /* Equidad: verificar que no se excedió W */
    if (c->config.flow_algo == FLOW_EQUIDAD) {
        if (st->passed_current >= c->config.equidad_w) return 0;
    }

    return 1;
}

/* Busca un barco en state.ships[] por puntero.
 * Retorna su índice o -1 si no está. Asume mutex tomado.  */
static int find_ship_slot(Canal *c, Ship *s)
{
    for (int i = 0; i < c->config.max_ships; i++)
        if (c->state.ships[i] == s) return i;
    return -1;
}

/* Despierta los barcos de una cola que ahora pueden entrar.
 * Llama sched_add() por cada uno que cumpla la condición.
 * Asume mutex tomado.
 *
 * ANTES: pthread_cond_broadcast() despertaba TODOS los
 *        barcos bloqueados, que luego competían por el mutex
 *        en pthread_cond_wait. El "perdedor" volvía a dormir.
 *
 * AHORA: solo se llama sched_add() para los barcos que
 *        realmente pueden entrar ahora. Los demás permanecen
 *        UTHREAD_BLOCKED y no consumen CPU.
 *
 * Esto es más eficiente: O(barcos elegibles) vs O(todos).  */
static void wake_eligible(Canal *c, ShipQueue *q)
{
    Ship *s = q->head;
    while (s) {
        Ship *next = s->next;
        if (can_enter_locked(c, s) && s->uth &&
            s->uth->state == UTHREAD_BLOCKED)
        {
            /* Sacar de la cola de espera del canal */
            //queue_remove(q, s);
            /* Devolver al scheduler como READY */
            s->uth->state = UTHREAD_READY;
            sched_add(g_scheduler, s->uth);
        }
        s = next;
    }
}

static void wake_ship_if_blocked(Ship *s)
{
    if (!s || !s->uth || !g_scheduler) {
        return;
    }

    if (s->uth->state == UTHREAD_BLOCKED) {
        s->uth->state = UTHREAD_READY;
        sched_add(g_scheduler, s->uth);
    }
}

/* =========================================================
 * SECCIÓN 2 — Inicialización y destrucción
 * ========================================================= */

Canal *canal_create(const CanalConfig *cfg)
{
    Canal *c = calloc(1, sizeof(Canal));
    if (!c) { perror("canal_create"); return NULL; }

    c->config = *cfg;
    if (c->config.max_ships <= 0)  c->config.max_ships  = 8;
    if (c->config.queue_visible <= 0) c->config.queue_visible = 4;

    c->state.direction = CANAL_DIR_FREE;

    pthread_mutex_init(&c->mutex, NULL);

    g_canal = c;
    return c;
}

void canal_destroy(Canal *c)
{
    if (!c) return;
    pthread_mutex_destroy(&c->mutex);
    free(c);
    if (g_canal == c) g_canal = NULL;
}


/* =========================================================
 * SECCIÓN 3 — Control de acceso (modelo uthread)
 * ========================================================= */

/* ---------------------------------------------------------
 * canal_can_enter
 *
 * Consultada por ship.c en un loop:
 *   while (!canal_can_enter(g_canal, s)) uthread_block();
 *
 * Toma el mutex, evalúa la condición y lo libera.
 * Retorna 1 si el barco puede entrar, 0 si debe esperar.
 * --------------------------------------------------------- */
int canal_can_enter(Canal *c, Ship *s)
{
    if (!c || !s) {
        return 0;
    }

    pthread_mutex_lock(&c->mutex);

    /*
     * Si el sensor/interrupcion esta activo,
     * nadie puede entrar al canal.
     */
    if (c->state.interrupted) {
        pthread_mutex_unlock(&c->mutex);
        return 0;
    }

    /*
     * Regla de seguridad:
     * solo un barco puede existir dentro del canal.
     *
     * Esto garantiza:
     * - no colisiones,
     * - no rebases,
     * - no barcos en sentidos opuestos,
     * - no dos barcos en la misma posicion.
     */
    if (c->state.ship_count > 0) {
        pthread_mutex_unlock(&c->mutex);
        return 0;
    }

    /*
     * TICO:
     * No hay control rigido de direccion.
     * Si el canal esta libre, puede entrar cualquier lado.
     */
    if (c->config.flow_algo == FLOW_TICO) {
        pthread_mutex_unlock(&c->mutex);
        return 1;
    }

    /*
     * Si el canal esta libre y no se ha fijado direccion,
     * puede entrar cualquier direccion.
     */
    if (c->state.direction == CANAL_DIR_FREE) {
        pthread_mutex_unlock(&c->mutex);
        return 1;
    }

    /*
     * LETRERO / EQUIDAD:
     * Respetar la direccion activa.
     */
    if (s->dir == DIR_LEFT && c->state.direction == CANAL_DIR_LEFT) {
        pthread_mutex_unlock(&c->mutex);
        return 1;
    }

    if (s->dir == DIR_RIGHT && c->state.direction == CANAL_DIR_RIGHT) {
        pthread_mutex_unlock(&c->mutex);
        return 1;
    }

    pthread_mutex_unlock(&c->mutex);
    return 0;
}


/* ---------------------------------------------------------
 * canal_enter_immediate
 *
 * Registra el barco en el canal SIN verificar condiciones.
 * PRE: canal_can_enter() retornó 1 justo antes de llamar.
 *
 * CAMBIO vs canal_enter():
 *   canal_enter() verificaba + esperaba + registraba.
 *   canal_enter_immediate() solo registra.
 *   La espera ahora vive en ship.c (uthread_block loop).
 * --------------------------------------------------------- */
void canal_enter_immediate(Canal *c, Ship *s)
{
    pthread_mutex_lock(&c->mutex);

    /* Encontrar slot libre */
    int slot = find_ship_slot(c, NULL);   /* NULL = slot vacío */
    if (slot == -1) {
        /* No debería pasar si canal_can_enter fue respetado,
         * pero defendemos el invariante igual.              */
        fprintf(stderr, "canal_enter_immediate: sin slots para barco %d\n",
                s->id);
        pthread_mutex_unlock(&c->mutex);
        return;
    }

    c->state.ships[slot] = s;
    c->state.ship_count++;

    /* Establecer dirección si el canal estaba libre */
    if (c->state.direction == CANAL_DIR_FREE) {
        c->state.direction = (s->dir == DIR_LEFT)
                             ? CANAL_DIR_LEFT
                             : CANAL_DIR_RIGHT;
    }

    /* Incrementar contador de barcos en la dirección actual
     * (usado por Equidad para saber cuándo rotar)          */
    c->state.passed_current++;

    pthread_mutex_unlock(&c->mutex);
}


/* ---------------------------------------------------------
 * canal_exit
 *
 * El barco terminó de cruzar. Lo elimina del canal y
 * despierta los barcos bloqueados elegibles.
 *
 * CAMBIO CLAVE:
 *   ANTES: pthread_cond_broadcast(&c->cond)
 *          → despertaba TODOS los barcos (thundering herd)
 *
 *   AHORA: wake_eligible() llama sched_add() solo para los
 *          barcos cuya condición se cumple EN ESTE MOMENTO
 *          → O(elegibles) en lugar de O(todos)
 * --------------------------------------------------------- */
void canal_exit(Canal *c, Ship *s)
{
    if (!c || !s) {
        return;
    }

    Ship *left_head = NULL;
    Ship *right_head = NULL;

    pthread_mutex_lock(&c->mutex);

    int found = -1;

    for (int i = 0; i < c->state.ship_count; i++) {
        if (c->state.ships[i] == s) {
            found = i;
            break;
        }
    }

    /*
     * Si no estaba en el canal, probablemente fue expulsado
     * por interrupción. No debe contar como cruzado.
     */
    if (found < 0) {
        pthread_mutex_unlock(&c->mutex);
        return;
    }

    for (int i = found; i < c->state.ship_count - 1; i++) {
        c->state.ships[i] = c->state.ships[i + 1];
    }

    c->state.ships[c->state.ship_count - 1] = NULL;
    c->state.ship_count--;

    c->state.total_crossed++;

    if (s->dir == DIR_LEFT) {
        c->state.total_left++;
    } else {
        c->state.total_right++;
    }

    /*
     * Como estamos usando política segura de un solo barco
     * dentro del canal, al salir queda libre.
     */
    if (c->state.ship_count == 0) {
        c->state.direction = CANAL_DIR_FREE;
        c->state.passed_current = 0;
    }

    /*
     * Guardar los barcos al frente de cada cola.
     * No llamamos sched_add() mientras tenemos el mutex del canal.
     */
    left_head = c->state.queue_left.head;
    right_head = c->state.queue_right.head;

    pthread_mutex_unlock(&c->mutex);

    /*
     * Despertar candidatos.
     * Si el flujo permite solo uno, el otro volverá a bloquearse
     * al ejecutar ship_try_enter_canal().
     */
    wake_ship_if_blocked(left_head);
    wake_ship_if_blocked(right_head);
}


/* ---------------------------------------------------------
 * canal_enter — DEPRECADA
 *
 * Mantenida para compatibilidad. Imprime advertencia en
 * stderr para detectar usos accidentales en tiempo de
 * ejecución. En el modelo uthread no debe llamarse.
 * --------------------------------------------------------- */
void canal_enter(Canal *c, Ship *s)
{
    fprintf(stderr,
        "[WARN] canal_enter() llamado para barco %d — "
        "función deprecada, usar canal_can_enter() + "
        "canal_enter_immediate()\n", s->id);

    /* Fallback de emergencia: busy-wait con yield.
     * No usar en producción — existe solo como red de
     * seguridad si algo llama canal_enter() por error.    */
    while (!canal_can_enter(c, s))
        uthread_yield();
    canal_enter_immediate(c, s);
}


/* =========================================================
 * SECCIÓN 4 — Control de flujo
 * ========================================================= */

/* ---------------------------------------------------------
 * canal_set_direction
 *
 * Cambia la dirección activa del canal (Letrero / Equidad).
 * Solo debe llamarse cuando el canal está vacío.
 * --------------------------------------------------------- */
void canal_set_direction(Canal *c, CanalDir dir)
{
    pthread_mutex_lock(&c->mutex);

    if (c->state.ship_count > 0) {
        /* No cambiar con barcos dentro — el llamador debe
         * esperar a que el canal se vacíe.                */
        pthread_mutex_unlock(&c->mutex);
        return;
    }

    c->state.direction      = dir;
    c->state.passed_current = 0;

    /* Despertar los barcos del nuevo lado activo */
    if (dir == CANAL_DIR_LEFT || dir == CANAL_DIR_FREE)
        wake_eligible(c, &c->state.queue_left);
    if (dir == CANAL_DIR_RIGHT || dir == CANAL_DIR_FREE)
        wake_eligible(c, &c->state.queue_right);

    pthread_mutex_unlock(&c->mutex);
}


/* ---------------------------------------------------------
 * canal_flow_controller — hilo de control de flujo
 *
 * Maneja Equidad y Tico. Este SÍ es un pthread legítimo
 * (infraestructura del canal, no un barco/proceso).
 *
 * CAMBIO vs versión anterior:
 *   ANTES: señalizaba con pthread_cond_broadcast al rotar.
 *   AHORA: llama canal_set_direction(), que internamente
 *          llama wake_eligible() con sched_add().
 * --------------------------------------------------------- */
void *canal_flow_controller(void *arg)
{
    Canal *c = (Canal *)arg;

    while (g_canal && g_canal == c) {

        if (c->config.flow_algo == FLOW_EQUIDAD) {
            pthread_mutex_lock(&c->mutex);

            /* Rotar si se alcanzó W y el canal está vacío */
            if (c->state.passed_current >= c->config.equidad_w
                && c->state.ship_count == 0)
            {
                CanalDir next = (c->state.direction == CANAL_DIR_LEFT)
                                ? CANAL_DIR_RIGHT
                                : CANAL_DIR_LEFT;

                /* Si el lado destino no tiene barcos,
                 * quedar en el lado actual (Equidad garantiza
                 * paso al lado con barcos).                 */
                ShipQueue *dest_q = (next == CANAL_DIR_LEFT)
                                    ? &c->state.queue_left
                                    : &c->state.queue_right;
                if (dest_q->count == 0) {
                    /* El otro lado está vacío — resetear
                     * contador pero no rotar aún.           */
                    c->state.passed_current = 0;
                    pthread_mutex_unlock(&c->mutex);
                    struct timespec ts;
                    ts.tv_sec = 0;
                    ts.tv_nsec = 5000000L;   /* 5 ms */
                    nanosleep(&ts, NULL);
                    continue;
                }

                c->state.direction      = next;
                c->state.passed_current = 0;

                /* Despertar barcos del nuevo lado */
                wake_eligible(c, dest_q);
            }
            pthread_mutex_unlock(&c->mutex);

        } else if (c->config.flow_algo == FLOW_TICO) {
            /* Tico: sin control explícito.
             * Solo garusleep(c->config.letrero_ms * 1000);antizar que si un lado tiene barcos
             * y el canal está libre, se les da paso.        */
            pthread_mutex_lock(&c->mutex);
            if (c->state.ship_count == 0 &&
                c->state.direction == CANAL_DIR_FREE)
            {
                /* Despertar cualquier barco esperando */
                wake_eligible(c, &c->state.queue_left);
                wake_eligible(c, &c->state.queue_right);
            }
            pthread_mutex_unlock(&c->mutex);
        }

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 5000000L;   /* 5 ms */
        nanosleep(&ts, NULL);  /* verificar cada 5ms */
    }
    return NULL;
}


/* ---------------------------------------------------------
 * canal_letrero_thread — hilo del Letrero
 *
 * Cambia la dirección cada letrero_ms milisegundos.
 * Espera a que el canal quede vacío antes de rotar.
 *
 * CAMBIO vs versión anterior:
 *   ANTES: pthread_cond_broadcast al cambiar dirección.
 *   AHORA: canal_set_direction() con wake_eligible().
 * --------------------------------------------------------- */
void *canal_letrero_thread(void *arg)
{
    Canal *c = (Canal *)arg;

    while (g_canal && g_canal == c) {
        

        /* Esperar a que el canal esté vacío */
        while (1) {
            pthread_mutex_lock(&c->mutex);
            int empty = (c->state.ship_count == 0);
            pthread_mutex_unlock(&c->mutex);
            if (empty) break;
            struct timespec ts;
            ts.tv_sec = c->config.letrero_ms / 1000;
            ts.tv_nsec = (c->config.letrero_ms % 1000) * 1000000L;
            nanosleep(&ts, NULL);
        }

        /* Rotar dirección */
        pthread_mutex_lock(&c->mutex);
        CanalDir next = (c->state.direction != CANAL_DIR_RIGHT)
                        ? CANAL_DIR_RIGHT
                        : CANAL_DIR_LEFT;
        pthread_mutex_unlock(&c->mutex);

        canal_set_direction(c, next);
    }
    return NULL;
}


/* =========================================================
 * SECCIÓN 5 — Interrupciones (sensor de proximidad)
 *
 * CAMBIO vs versión anterior:
 *   ANTES: los barcos expulsados hacían pthread_cond_wait
 *          en canal_enter() hasta ser re-admitidos.
 *   AHORA: se marcan SHIP_BLOCKED + UTHREAD_BLOCKED y se
 *          reinsertan a la cola del canal. canal_exit()
 *          los activará via wake_eligible() cuando el
 *          canal se despeje.
 * ========================================================= */
static void queue_push_front(ShipQueue *q, Ship *s)
{
    if (!q || !s) return;

    s->next = q->head;
    q->head = s;

    if (q->tail == NULL) {
        q->tail = s;
    }

    q->count++;
}

void canal_interrupt(Canal *c)
{
    if (!c) return;

    pthread_mutex_lock(&c->mutex);

    /*
     * Si ya está interrumpido, no volver a reinsertar barcos.
     * Esto evita duplicados si el sensor manda SENSOR:1 varias veces.
     */
    if (c->state.interrupted) {
        pthread_mutex_unlock(&c->mutex);
        return;
    }

    c->state.interrupted = 1;

    /*
     * Expulsar todos los barcos que están dentro del canal.
     * Se devuelven al frente de la cola correspondiente.
     *
     * IMPORTANTE:
     * Solo se cambia s->state.
     * NO se cambia s->uth->state aquí.
     */
    for (int i = 0; i < c->state.ship_count; i++) {
        Ship *s = c->state.ships[i];

        if (!s) {
            continue;
        }

        s->state = SHIP_BLOCKED;

        if (s->dir == DIR_LEFT) {
            s->pos = 0;
            queue_push_front(&c->state.queue_left, s);
        } else {
            s->pos = s->canal_len;
            queue_push_front(&c->state.queue_right, s);
        }

        c->state.ships[i] = NULL;
    }

    c->state.ship_count = 0;
    c->state.direction = CANAL_DIR_FREE;
    c->state.passed_current = 0;

    /*
     * NO limpiar interrupted aquí.
     * Se limpia con canal_clear_interrupt().
     */

    pthread_mutex_unlock(&c->mutex);
}

void canal_clear_interrupt(Canal *c)
{
    if (!c) return;

    pthread_mutex_lock(&c->mutex);

    c->state.interrupted = 0;

    /*
     * Al liberar la interrupción, despertar barcos elegibles
     * para que vuelvan a competir por entrar al canal.
     */
    wake_eligible(c, &c->state.queue_left);
    wake_eligible(c, &c->state.queue_right);

    pthread_mutex_unlock(&c->mutex);
}
/* =========================================================
 * SECCIÓN 6 — Carga de configuración
 * ========================================================= */

FlowAlgo flow_algo_from_str(const char *str)
{
    if (!str) return FLOW_TICO;

    if (strcmp(str, "EQUIDAD") == 0) return FLOW_EQUIDAD;
    if (strcmp(str, "LETRERO") == 0) return FLOW_LETRERO;
    if (strcmp(str, "TICO")    == 0) return FLOW_TICO;
    fprintf(stderr, "flow_algo_from_str: algoritmo desconocido '%s'\n", str);
    return FLOW_EQUIDAD;   /* default seguro */
}

static void trim(char *s)
{
    if (!s) return;

    /* Quitar espacios iniciales */
    char *start = s;
    while (*start == ' ' || *start == '\t') {
        start++;
    }

    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    /* Quitar espacios finales, \n y \r */
    size_t n = strlen(s);

    while (n > 0 &&
           (s[n - 1] == ' '  ||
            s[n - 1] == '\t' ||
            s[n - 1] == '\n' ||
            s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

Canal *canal_load_config(const char *filepath)
{
    FILE *f = fopen(filepath, "r");
    if (!f) {
        perror("canal_load_config: fopen");
        return NULL;
    }

    CanalConfig cfg = {
        .flow_algo     = FLOW_TICO,
        .canal_length  = 12,
        .max_ships     = 1,
        .queue_visible = 4,
        .letrero_ms    = 4000,
        .equidad_w     = 3,
    };

    char line[256];

    while (fgets(line, sizeof(line), f)) {
        trim(line);

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }

        *eq = '\0';

        char *key = line;
        char *val = eq + 1;

        trim(key);
        trim(val);

        if (strcmp(key, "flow_algo") == 0) {
            cfg.flow_algo = flow_algo_from_str(val);
        } else if (strcmp(key, "canal_length") == 0) {
            cfg.canal_length = atoi(val);
        } else if (strcmp(key, "max_ships") == 0) {
            cfg.max_ships = atoi(val);
        } else if (strcmp(key, "queue_visible") == 0) {
            cfg.queue_visible = atoi(val);
        } else if (strcmp(key, "letrero_ms") == 0) {
            cfg.letrero_ms = atoi(val);
        } else if (strcmp(key, "equidad_w") == 0) {
            cfg.equidad_w = atoi(val);
        }
    }

    fclose(f);

    if (cfg.queue_visible <= 0 || cfg.queue_visible > 4) {
        cfg.queue_visible = 4;
    }

    if (cfg.max_ships <= 0) {
        cfg.max_ships = 1;
    }

    return canal_create(&cfg);
}