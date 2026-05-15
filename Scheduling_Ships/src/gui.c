#include "gui.h"
#include "scheduler.h"

#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* =========================================================
 * gui.c — Interfaz ncurses compatible con Canal nuevo
 *
 * Cambios principales:
 *   c->cfg              -> c->config
 *   c->lock             -> c->mutex
 *   c->queue_left       -> c->state.queue_left
 *   c->queue_right      -> c->state.queue_right
 *   c->in_canal         -> c->state.ships
 *   c->in_canal_count   -> c->state.ship_count
 *   c->current_dir      -> c->state.direction
 *   STATE_CROSSING      -> SHIP_CROSSING
 *   SHIP_FISHING        -> SHIP_FISHER
 * ========================================================= */

static void gui_sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void gui_init(GuiState *g, Canal *c)
{
    g->canal   = c;
    g->running = 1;
    memset(g->log_msg, 0, sizeof(g->log_msg));

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    getmaxyx(stdscr, g->rows, g->cols);

    if (has_colors()) {
        start_color();
        init_pair(COLOR_NORMAL_SHIP,  COLOR_WHITE, COLOR_BLUE);
        init_pair(COLOR_FISHING_SHIP, COLOR_BLACK, COLOR_GREEN);
        init_pair(COLOR_PATROL_SHIP,  COLOR_BLACK, COLOR_RED);
        init_pair(COLOR_CANAL_BG,     COLOR_WHITE, COLOR_CYAN);
        init_pair(COLOR_QUEUE_BG,     COLOR_BLACK, COLOR_YELLOW);
        init_pair(COLOR_HEADER,       COLOR_BLACK, COLOR_WHITE);
        init_pair(COLOR_STATUS,       COLOR_GREEN, COLOR_BLACK);
    }
}

void gui_destroy(GuiState *g)
{
    (void)g;
    endwin();
}

void gui_log(GuiState *g, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g->log_msg, sizeof(g->log_msg), fmt, ap);
    va_end(ap);
}

void gui_draw_header(GuiState *g)
{
    attron(COLOR_PAIR(COLOR_HEADER) | A_BOLD);

    const char *title = " SCHEDULING SHIPS - CE 4303 - TEC ";
    int col = (g->cols - (int)strlen(title)) / 2;
    if (col < 0) col = 0;

    mvprintw(0, 0, "%*s", g->cols, "");
    mvprintw(0, col, "%s", title);

    attroff(COLOR_PAIR(COLOR_HEADER) | A_BOLD);

    Canal *c = g->canal;

    const char *flow_str =
        c->config.flow_algo == FLOW_EQUIDAD ? "EQUIDAD" :
        c->config.flow_algo == FLOW_LETRERO ? "LETRERO" :
        "TICO";

    const char *sched_str = g_scheduler
                            ? sched_algo_name(g_scheduler->algo)
                            : "N/A";

    mvprintw(1, 2,
             "Flujo: %-8s | Scheduler: %-8s | Canal: %d unidades | Visible cola: %d | Max canal: %d",
             flow_str,
             sched_str,
             c->config.canal_length,
             c->config.queue_visible,
             c->config.max_ships);

    if (g_scheduler && g_scheduler->algo == SS_RR) {
        printw(" | Quantum: %d ms", g_scheduler->quantum_ms);
    }

    mvprintw(2, 2, "Tipos: ");
    attron(COLOR_PAIR(COLOR_NORMAL_SHIP));
    printw("[N]ormal  ");
    attroff(COLOR_PAIR(COLOR_NORMAL_SHIP));

    attron(COLOR_PAIR(COLOR_FISHING_SHIP));
    printw("[F]Pesquero  ");
    attroff(COLOR_PAIR(COLOR_FISHING_SHIP));

    attron(COLOR_PAIR(COLOR_PATROL_SHIP));
    printw("[P]atrulla");
    attroff(COLOR_PAIR(COLOR_PATROL_SHIP));

    mvhline(3, 0, ACS_HLINE, g->cols);
}

void gui_draw_letrero(GuiState *g)
{
    Canal *c = g->canal;

    pthread_mutex_lock(&c->mutex);
    CanalDir dir = c->state.direction;
    int interrupted = c->state.interrupted;
    pthread_mutex_unlock(&c->mutex);

    const char *letrero;
    int color;

    if (interrupted) {
        letrero = "[ INTERRUPCION / SENSOR ACTIVO ]";
        color = COLOR_PATROL_SHIP;
    } else if (dir == CANAL_DIR_LEFT) {
        letrero = "[ IZQ --> DER ]";
        color = COLOR_NORMAL_SHIP;
    } else if (dir == CANAL_DIR_RIGHT) {
        letrero = "[ DER <-- IZQ ]";
        color = COLOR_PATROL_SHIP;
    } else {
        letrero = "[    LIBRE    ]";
        color = COLOR_STATUS;
    }

    int col = (g->cols - (int)strlen(letrero)) / 2;
    if (col < 0) col = 0;

    attron(COLOR_PAIR(color) | A_BOLD);
    mvprintw(4, col, "%s", letrero);
    attroff(COLOR_PAIR(color) | A_BOLD);
}

static void print_ship_char(Ship *s)
{
    int color;
    char sym;

    switch (s->type) {
        case SHIP_NORMAL:
            color = COLOR_NORMAL_SHIP;
            sym = 'N';
            break;

        case SHIP_FISHER:
            color = COLOR_FISHING_SHIP;
            sym = 'F';
            break;

        case SHIP_PATROL:
            color = COLOR_PATROL_SHIP;
            sym = 'P';
            break;

        default:
            color = COLOR_STATUS;
            sym = '?';
            break;
    }

    attron(COLOR_PAIR(color) | A_BOLD);
    printw("[%c%d]", sym, s->id);
    attroff(COLOR_PAIR(color) | A_BOLD);
}

void gui_draw_queues(GuiState *g)
{
    Canal *c = g->canal;
    int row = GUI_CANAL_ROW_TOP - 2;

    pthread_mutex_lock(&c->mutex);

    mvprintw(row, 0, "Cola IZQ [%d]: ", c->state.queue_left.count);

    Ship *cur = c->state.queue_left.head;
    int off = 14;
    int printed = 0;

    while (cur && off < g->cols / 3 && printed < c->config.queue_visible) {
        move(row, off);
        print_ship_char(cur);
        off += 5;
        cur = cur->next;
        printed++;
    }

    int rstart = g->cols * 2 / 3;

    mvprintw(row, rstart, "Cola DER [%d]: ", c->state.queue_right.count);

    cur = c->state.queue_right.head;
    off = rstart + 14;
    printed = 0;

    while (cur && off < g->cols - 4 && printed < c->config.queue_visible) {
        move(row, off);
        print_ship_char(cur);
        off += 5;
        cur = cur->next;
        printed++;
    }

    pthread_mutex_unlock(&c->mutex);
}

void gui_draw_canal(GuiState *g)
{
    Canal *c = g->canal;
    int len = c->config.canal_length;

    if (len <= 0) {
        len = 1;
    }

    int cs = (g->cols - len * 3 - 4) / 2;
    if (cs < 2) cs = 2;

    int ce = cs + len * 3 + 2;

    int rt = GUI_CANAL_ROW_TOP;
    int rb = GUI_CANAL_ROW_BOT;

    attron(COLOR_PAIR(COLOR_CANAL_BG));

    mvprintw(rt - 1, cs, "+");
    for (int i = cs + 1; i < ce; i++) mvaddch(rt - 1, i, '-');
    mvprintw(rt - 1, ce, "+");

    mvprintw(rt, cs, "|");
    for (int i = cs + 1; i < ce; i++) mvaddch(rt, i, ' ');
    mvprintw(rt, ce, "|");

    mvprintw(rb, cs, "|");
    for (int i = cs + 1; i < ce; i++) mvaddch(rb, i, ' ');
    mvprintw(rb, ce, "|");

    mvprintw(rb + 1, cs, "+");
    for (int i = cs + 1; i < ce; i++) mvaddch(rb + 1, i, '-');
    mvprintw(rb + 1, ce, "+");

    attroff(COLOR_PAIR(COLOR_CANAL_BG));

    mvprintw(rt, cs - 5, "-->");
    mvprintw(rb, ce + 2, "<--");

    pthread_mutex_lock(&c->mutex);

    for (int i = 0; i < c->state.ship_count; i++) {
        Ship *s = c->state.ships[i];

        if (!s || s->state != SHIP_CROSSING) {
            continue;
        }

        int pos = s->pos;
        int mx = s->canal_len;

        if (mx <= 0) {
            mx = len;
        }

        int sc;

        if (s->dir == DIR_LEFT) {
            sc = cs + 1 + (pos * (len * 3)) / mx;

            if (sc > ce - 4) {
                sc = ce - 4;
            }

            move(rt, sc);
        } else {
            /*
             * Para barcos de derecha a izquierda, pos va bajando
             * desde canal_len hasta 0.
             */
            sc = cs + 1 + (pos * (len * 3)) / mx;

            if (sc < cs + 2) {
                sc = cs + 2;
            }

            if (sc > ce - 4) {
                sc = ce - 4;
            }

            move(rb, sc);
        }

        print_ship_char(s);
    }

    pthread_mutex_unlock(&c->mutex);
}

void gui_draw_stats(GuiState *g)
{
    Canal *c = g->canal;

    pthread_mutex_lock(&c->mutex);

    long total = c->state.total_crossed;
    long tl = c->state.total_left;
    long tr = c->state.total_right;
    int in_now = c->state.ship_count;
    int ql = c->state.queue_left.count;
    int qr = c->state.queue_right.count;
    int interrupted = c->state.interrupted;

    pthread_mutex_unlock(&c->mutex);

    int row = GUI_STATUS_ROW;

    mvhline(row, 0, ACS_HLINE, g->cols);

    attron(COLOR_PAIR(COLOR_STATUS));

    mvprintw(row + 1, 2,
             "Cruzaron: %ld (L->R:%ld  R->L:%ld) | En canal: %d | Cola L:%d  R:%d | Sensor:%s",
             total,
             tl,
             tr,
             in_now,
             ql,
             qr,
             interrupted ? "ACTIVO" : "NO");

    if (g_scheduler) {
        mvprintw(row + 2, 2,
                 "Sched: %s | scheduled:%ld | preemptions:%ld | idle:%ld",
                 sched_algo_name(g_scheduler->algo),
                 g_scheduler->metrics.total_scheduled,
                 g_scheduler->metrics.total_preemptions,
                 g_scheduler->metrics.total_idle);
    }

    attroff(COLOR_PAIR(COLOR_STATUS));

    if (g->log_msg[0]) {
        mvprintw(row + 3, 2, ">> %s", g->log_msg);
    }
}

void gui_draw_help(GuiState *g)
{
    int row = GUI_HELP_ROW;

    mvhline(row, 0, ACS_HLINE, g->cols);

    mvprintw(row + 1, 2,
             "Teclas: [n] Normal IZQ  [N] Normal DER  [f] Pesquero IZQ  [F] Pesquero DER");

    mvprintw(row + 2, 2,
             "        [p] Patrulla IZQ  [P] Patrulla DER  [i] Interrupcion  [w] Salir");
}

void gui_draw(GuiState *g)
{
    clear();

    getmaxyx(stdscr, g->rows, g->cols);

    gui_draw_header(g);
    gui_draw_letrero(g);
    gui_draw_queues(g);
    gui_draw_canal(g);
    gui_draw_stats(g);
    gui_draw_help(g);

    refresh();
}

void *gui_render_thread(void *arg)
{
    GuiState *g = (GuiState *)arg;

    while (g->running && g_scheduler && g_scheduler->active) {
        gui_draw(g);
        gui_sleep_ms(50);
    }

    return NULL;
}
static int gui_queue_has_space(GuiState *g, ShipDir dir)
{
    Canal *c = g->canal;

    if (!c) {
        return 0;
    }

    pthread_mutex_lock(&c->mutex);

    ShipQueue *q = (dir == DIR_LEFT)
                   ? &c->state.queue_left
                   : &c->state.queue_right;

    int limit = c->config.queue_visible;

    if (limit <= 0 || limit > 4) {
        limit = 4;
    }

    int has_space = q->count < limit;

    pthread_mutex_unlock(&c->mutex);

    return has_space;
}

static int gui_create_ship_checked(GuiState *g,
                                   ShipType type,
                                   ShipDir dir,
                                   int priority,
                                   int burst_ms,
                                   int deadline_ms,
                                   const char *msg)
{
    if (!gui_queue_has_space(g, dir)) {
        gui_log(g,
                "Cola %s llena: maximo 4 barcos.",
                (dir == DIR_LEFT) ? "izquierda" : "derecha");
        return 0;
    }

    Ship *s = ship_create(type, dir, priority, burst_ms, deadline_ms);

    if (!s) {
        gui_log(g,
                "No se pudo crear barco: cola %s llena.",
                (dir == DIR_LEFT) ? "izquierda" : "derecha");
        return 0;
    }

    gui_log(g, "%s", msg);
    return 1;
}

int gui_handle_input(GuiState *g)
{
    int ch = getch();

    if (ch == ERR) {
        return 0;
    }

    switch (ch) {
        case 'w':
        case 'W':
            g->running = 0;

            if (g_scheduler) {
                sched_shutdown(g_scheduler);
            }

            return 1;

        case 'n':
            gui_create_ship_checked(g,
                                    SHIP_NORMAL,
                                    DIR_LEFT,
                                    0,
                                    0,
                                    0,
                                    "Barco Normal L->R");
            break;

        case 'N':
            gui_create_ship_checked(g,
                                    SHIP_NORMAL,
                                    DIR_RIGHT,
                                    0,
                                    0,
                                    0,
                                    "Barco Normal R->L");
            break;

        case 'f':
            gui_create_ship_checked(g,
                                    SHIP_FISHER,
                                    DIR_LEFT,
                                    0,
                                    0,
                                    0,
                                    "Barco Pesquero L->R");
            break;

        case 'F':
            gui_create_ship_checked(g,
                                    SHIP_FISHER,
                                    DIR_RIGHT,
                                    0,
                                    0,
                                    0,
                                    "Barco Pesquero R->L");
            break;

        case 'p': {
            int burst = ship_default_burst(SHIP_PATROL,
                                        g->canal->config.canal_length);

            gui_create_ship_checked(g,
                                    SHIP_PATROL,
                                    DIR_LEFT,
                                    0,
                                    burst,
                                    burst,
                                    "Barco Patrulla L->R (URGENTE)");
            break;
        }

        case 'P': {
            int burst = ship_default_burst(SHIP_PATROL,
                                        g->canal->config.canal_length);

            gui_create_ship_checked(g,
                                    SHIP_PATROL,
                                    DIR_RIGHT,
                                    0,
                                    burst,
                                    burst,
                                    "Barco Patrulla R->L (URGENTE)");
            break;
        }
        case 'i':
        case 'I':
            gui_log(g, "Interrupcion por sensor de proximidad");
            canal_interrupt(g->canal);
            break;

        default:
            break;
    }

    return 0;
}