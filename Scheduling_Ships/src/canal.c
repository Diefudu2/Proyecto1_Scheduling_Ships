#include "canal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void canal_init(Canal *c, CanalConfig *cfg) {
    memset(c, 0, sizeof(*c));
    c->cfg         = *cfg;
    c->current_dir = CANAL_DIR_FREE;
    c->running     = 1;

    queue_init(&c->queue_left);
    queue_init(&c->queue_right);

    pthread_mutex_init(&c->lock, NULL);
    pthread_cond_init(&c->can_enter, NULL);
}

void canal_destroy(Canal *c) {
    queue_destroy(&c->queue_left);
    queue_destroy(&c->queue_right);
    pthread_mutex_destroy(&c->lock);
    pthread_cond_destroy(&c->can_enter);
}

int canal_load_config(CanalConfig *cfg, const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) { perror("canal_load_config"); return -1; }

    /* Defaults */
    cfg->flow_algo       = FLOW_TICO;
    cfg->canal_length    = 10;
    cfg->ship_speed      = 1;
    cfg->max_queue       = 4;
    cfg->letrero_time_ms = 3000;
    cfg->equidad_w       = 2;

    char key[64], val[64];
    while (fscanf(f, " %63[^=]=%63s", key, val) == 2) {
        char *k = key;
        while (*k == ' ' || *k == '\t') k++;

        if      (strcmp(k, "flow_algo")       == 0) {
            if      (strcmp(val, "EQUIDAD") == 0) cfg->flow_algo = FLOW_EQUIDAD;
            else if (strcmp(val, "LETRERO") == 0) cfg->flow_algo = FLOW_LETRERO;
            else                                   cfg->flow_algo = FLOW_TICO;
        }
        else if (strcmp(k, "canal_length")    == 0) cfg->canal_length    = atoi(val);
        else if (strcmp(k, "ship_speed")      == 0) cfg->ship_speed      = atoi(val);
        else if (strcmp(k, "max_queue")       == 0) cfg->max_queue       = atoi(val);
        else if (strcmp(k, "letrero_time_ms") == 0) cfg->letrero_time_ms = atoi(val);
        else if (strcmp(k, "equidad_w")       == 0) cfg->equidad_w       = atoi(val);
    }

    fclose(f);
    return 0;
}

int canal_save_config_template(const char *filepath) {
    FILE *f = fopen(filepath, "w");
    if (!f) { perror("canal_save_config_template"); return -1; }

    fprintf(f,
        "# Archivo de configuracion del canal\n"
        "# CE 4303 - Scheduling Ships\n"
        "# flow_algo: TICO | LETRERO | EQUIDAD\n"
        "flow_algo=TICO\n"
        "canal_length=12\n"
        "ship_speed=1\n"
        "max_queue=4\n"
        "letrero_time_ms=4000\n"
        "equidad_w=3\n"
    );
    fclose(f);
    return 0;
}

int canal_can_enter(Canal *c, Ship *s) {
    if (c->in_canal_count == 0) return 1;

    CanalDirection ship_dir = (s->direction == DIR_LEFT)
                              ? CANAL_DIR_LEFT : CANAL_DIR_RIGHT;

    if (c->current_dir == CANAL_DIR_FREE) return 1;
    if (c->current_dir != ship_dir)       return 0;

    if (c->cfg.flow_algo == FLOW_EQUIDAD) {
        if (c->ships_passed_current_dir >= c->cfg.equidad_w) return 0;
    }

    return 1;
}

void canal_enter(Canal *c, Ship *s) {
    pthread_mutex_lock(&c->lock);

    while (!canal_can_enter(c, s) && c->running)
        pthread_cond_wait(&c->can_enter, &c->lock);

    if (!c->running) {
        pthread_mutex_unlock(&c->lock);
        pthread_exit(NULL);
    }

    c->in_canal[c->in_canal_count++] = s;
    c->current_dir = (s->direction == DIR_LEFT)
                     ? CANAL_DIR_LEFT : CANAL_DIR_RIGHT;
    c->ships_passed_current_dir++;

    pthread_mutex_unlock(&c->lock);
}

void canal_exit(Canal *c, Ship *s) {
    pthread_mutex_lock(&c->lock);

    for (int i = 0; i < c->in_canal_count; i++) {
        if (c->in_canal[i] == s) {
            c->in_canal[i] = c->in_canal[c->in_canal_count - 1];
            c->in_canal[c->in_canal_count - 1] = NULL;
            c->in_canal_count--;
            break;
        }
    }

    c->total_crossed++;
    if (s->direction == DIR_LEFT) c->total_left++;
    else                          c->total_right++;

    if (c->in_canal_count == 0)
        c->current_dir = CANAL_DIR_FREE;

    pthread_cond_broadcast(&c->can_enter);
    pthread_mutex_unlock(&c->lock);
}

void *canal_letrero_thread(void *arg) {
    Canal *c = (Canal *)arg;

    while (c->running) {
        usleep((unsigned int)(c->cfg.letrero_time_ms * 1000));
        if (!c->running) break;

        pthread_mutex_lock(&c->lock);
        if (c->in_canal_count == 0) {
            if (c->current_dir == CANAL_DIR_LEFT ||
                c->current_dir == CANAL_DIR_FREE) {
                c->current_dir = CANAL_DIR_RIGHT;
            } else {
                c->current_dir = CANAL_DIR_LEFT;
            }
            c->ships_passed_current_dir = 0;
            pthread_cond_broadcast(&c->can_enter);
        }
        pthread_mutex_unlock(&c->lock);
    }
    return NULL;
}

void *canal_flow_controller(void *arg) {
    Canal *c = (Canal *)arg;

    while (c->running) {
        usleep(50000);
        if (!c->running) break;

        pthread_mutex_lock(&c->lock);

        if (c->cfg.flow_algo == FLOW_EQUIDAD && c->in_canal_count == 0) {
            if (c->ships_passed_current_dir >= c->cfg.equidad_w) {
                c->current_dir = (c->current_dir == CANAL_DIR_LEFT)
                                 ? CANAL_DIR_RIGHT : CANAL_DIR_LEFT;
                c->ships_passed_current_dir = 0;
                pthread_cond_broadcast(&c->can_enter);
            } else {
                int has_left  = (c->queue_left.count  > 0);
                int has_right = (c->queue_right.count > 0);
                if (has_left && !has_right) {
                    c->current_dir = CANAL_DIR_LEFT;
                    pthread_cond_broadcast(&c->can_enter);
                } else if (has_right && !has_left) {
                    c->current_dir = CANAL_DIR_RIGHT;
                    pthread_cond_broadcast(&c->can_enter);
                }
            }
        }

        if (c->cfg.flow_algo == FLOW_TICO && c->in_canal_count == 0) {
            c->current_dir = CANAL_DIR_FREE;
            pthread_cond_broadcast(&c->can_enter);
        }

        pthread_mutex_unlock(&c->lock);
    }
    return NULL;
}

void canal_print_status(Canal *c) {
    pthread_mutex_lock(&c->lock);
    printf("[CANAL] Dir: %s | En canal: %d | Total: %d (L:%d R:%d)\n",
           c->current_dir == CANAL_DIR_LEFT  ? "IZQ->DER" :
           c->current_dir == CANAL_DIR_RIGHT ? "DER<-IZQ" : "LIBRE",
           c->in_canal_count,
           c->total_crossed, c->total_left, c->total_right);
    pthread_mutex_unlock(&c->lock);
}