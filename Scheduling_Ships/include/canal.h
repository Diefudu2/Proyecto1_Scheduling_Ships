#ifndef CANAL_H
#define CANAL_H

#include "ship.h"
#include <pthread.h>

/* =========================================================
 * canal.h — Monitor de sincronización del canal
 *
 * CAMBIOS respecto a la versión anterior:
 *
 *   + canal_enter_immediate() — nueva función para el modelo
 *     uthread: registra un barco sin bloquear. El llamador
 *     (ship.c) garantiza que canal_can_enter() ya fue
 *     verificado antes de llamarla.
 *
 *   ~ canal_enter() — DEPRECADA. Bloqueaba internamente con
 *     pthread_cond_wait, incompatible con el scheduler propio.
 *     Se mantiene declarada para compatibilidad pero no debe
 *     usarse en código nuevo.
 *
 *   ~ canal_exit() — firma sin cambios. Internamente ahora
 *     llama sched_add() en lugar de pthread_cond_signal().
 *
 *   - pthread_cond_t en CanalState — eliminada. Ya no se
 *     necesita; la sincronización la maneja el scheduler.
 * ========================================================= */


/* ---------------------------------------------------------
 * Algoritmos de control de flujo
 * --------------------------------------------------------- */
typedef enum {
    FLOW_EQUIDAD = 0,   /* W barcos por lado, alternado        */
    FLOW_LETRERO = 1,   /* cambia de dirección cada T segundos */
    FLOW_TICO    = 2    /* sin control explícito, sin colisión */
} FlowAlgo;


/* ---------------------------------------------------------
 * Dirección activa del canal
 * --------------------------------------------------------- */
typedef enum {
    CANAL_DIR_LEFT  = 0,   /* izquierda -> derecha */
    CANAL_DIR_RIGHT = 1,   /* derecha -> izquierda */
    CANAL_DIR_FREE  = 2    /* sin barcos, cualquier dirección */
} CanalDir;


/* ---------------------------------------------------------
 * Configuración leída desde canal.cfg
 * --------------------------------------------------------- */
typedef struct {
    FlowAlgo    flow_algo;
    int         canal_length;    /* largo del canal en unidades       */
    int         max_ships;       /* max barcos simultáneos, default 8 */
    int         queue_visible;   /* barcos visibles en cola, max 4    */
    int         letrero_ms;      /* ms entre cambios de letrero       */
    int         equidad_w;       /* barcos por lado en Equidad        */
} CanalConfig;


/* ---------------------------------------------------------
 * Estado dinámico del canal
 * --------------------------------------------------------- */
typedef struct {
    Ship       *ships[8];        /* barcos actualmente en el canal    */
    int         ship_count;      /* cuántos hay ahora                 */
    CanalDir    direction;       /* dirección activa                  */
    int         passed_current;  /* barcos pasados en dirección actual*/

    ShipQueue   queue_left;      /* cola de espera lado izquierdo     */
    ShipQueue   queue_right;     /* cola de espera lado derecho       */

    int         interrupted;     /* 1 = sensor activo, canal bloqueado*/

    /* Métricas */
    long        total_crossed;
    long        total_left;
    long        total_right;
} CanalState;


/* ---------------------------------------------------------
 * Estructura principal
 * --------------------------------------------------------- */
typedef struct {
    CanalConfig     config;
    CanalState      state;

    /*
     * Mutex protege state completo.
     * Ya no hay pthread_cond_t: la sincronización de barcos
     * esperando la maneja el scheduler mediante
     * UTHREAD_BLOCKED + sched_add().
     */
    pthread_mutex_t mutex;

    /*
     * Hilos auxiliares del canal.
     * Estos sí son pthread legítimos porque no representan
     * barcos/procesos simulados.
     */
    pthread_t       letrero_thread;
    pthread_t       flow_thread;
} Canal;


/* ---------------------------------------------------------
 * Global — accesible desde ship.c, gui.c y main.c
 * --------------------------------------------------------- */
extern Canal *g_canal;


/* =========================================================
 * API pública
 * ========================================================= */

/* ---------------------------------------------------------
 * Inicialización y destrucción
 * --------------------------------------------------------- */
Canal *canal_create(const CanalConfig *cfg);
void   canal_destroy(Canal *c);


/* ---------------------------------------------------------
 * Control de acceso — modelo UThread
 *
 * Flujo correcto en ship.c:
 *
 *   while (!canal_can_enter(g_canal, s))
 *       uthread_block();
 *
 *   canal_enter_immediate(g_canal, s);
 *
 *   // cruzar canal
 *
 *   canal_exit(g_canal, s);
 * --------------------------------------------------------- */

/*
 * canal_can_enter — consulta si el barco puede entrar ahora.
 *
 * Verifica dirección activa, capacidad y estado del canal.
 * No bloquea. Toma y libera el mutex internamente.
 * Retorna 1 si puede entrar, 0 si debe esperar.
 */
int canal_can_enter(Canal *c, Ship *s);


/*
 * canal_enter_immediate — registra el barco en el canal.
 *
 * PRE: canal_can_enter() retornó 1 en la iteración previa.
 * Registra el barco en state.ships[], actualiza dirección
 * y contadores. No bloquea y no verifica condiciones.
 */
void canal_enter_immediate(Canal *c, Ship *s);


/*
 * canal_exit — el barco termina de cruzar.
 *
 * Elimina el barco de state.ships[] y actualiza métricas.
 * Luego despierta barcos bloqueados que ahora pueden entrar
 * usando sched_add().
 */
void canal_exit(Canal *c, Ship *s);


/*
 * canal_enter — deprecada.
 *
 * Bloqueaba con pthread_cond_wait. Es incompatible con el
 * modelo UThread. No usar en código nuevo.
 */
void canal_enter(Canal *c, Ship *s)
    __attribute__((deprecated("usar canal_can_enter() + canal_enter_immediate()")));


/* ---------------------------------------------------------
 * Control de flujo
 * --------------------------------------------------------- */
void *canal_flow_controller(void *arg);
void *canal_letrero_thread(void *arg);
void  canal_set_direction(Canal *c, CanalDir dir);


/* ---------------------------------------------------------
 * Interrupciones — sensor de proximidad
 *
 * canal_interrupt() expulsa todos los barcos del canal,
 * los marca como bloqueados y los reinserta para que el
 * scheduler los vuelva a considerar.
 * --------------------------------------------------------- */
void canal_interrupt(Canal *c);


/* ---------------------------------------------------------
 * Helpers de configuración
 * --------------------------------------------------------- */
Canal   *canal_load_config(const char *filepath);
FlowAlgo flow_algo_from_str(const char *str);

#endif /* CANAL_H */