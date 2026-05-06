#ifndef CANAL_H
#define CANAL_H

#include "ship.h"
#include <pthread.h>

typedef enum {
    FLOW_EQUIDAD = 0,
    FLOW_LETRERO = 1,
    FLOW_TICO    = 2
} FlowAlgo;

typedef enum {
    CANAL_DIR_LEFT  = 0,
    CANAL_DIR_RIGHT = 1,
    CANAL_DIR_FREE  = 2
} CanalDirection;

typedef struct {
    FlowAlgo  flow_algo;
    int       canal_length;
    int       ship_speed;
    int       max_queue;
    int       letrero_time_ms;
    int       equidad_w;
} CanalConfig;

typedef struct {
    CanalConfig    cfg;

    Ship          *in_canal[8];
    int            in_canal_count;

    CanalDirection current_dir;
    int            ships_passed_current_dir;

    ShipQueue      queue_left;
    ShipQueue      queue_right;

    pthread_mutex_t lock;
    pthread_cond_t  can_enter;

    int             running;

    int             total_crossed;
    int             total_left;
    int             total_right;
} Canal;

void  canal_init               (Canal *c, CanalConfig *cfg);
void  canal_destroy            (Canal *c);
int   canal_load_config        (CanalConfig *cfg, const char *filepath);
int   canal_save_config_template(const char *filepath);
int   canal_can_enter          (Canal *c, Ship *s);
void  canal_enter              (Canal *c, Ship *s);
void  canal_exit               (Canal *c, Ship *s);
void *canal_letrero_thread     (void *arg);
void *canal_flow_controller    (void *arg);
void  canal_print_status       (Canal *c);

#endif 