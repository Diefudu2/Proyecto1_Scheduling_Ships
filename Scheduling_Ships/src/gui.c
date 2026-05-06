#include "gui.h"
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void gui_init(GuiState *g, Canal *c) {
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

void gui_destroy(GuiState *g) {
    (void)g;
    endwin();
}

void gui_log(GuiState *g, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g->log_msg, sizeof(g->log_msg), fmt, ap);
    va_end(ap);
}

void gui_draw_header(GuiState *g) {
    attron(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
    const char *title = " SCHEDULING SHIPS - CE 4303 - TEC ";
    int col = (g->cols - (int)strlen(title)) / 2;
    if (col < 0) col = 0;
    mvprintw(0, 0, "%*s", g->cols, "");
    mvprintw(0, col, "%s", title);
    attroff(COLOR_PAIR(COLOR_HEADER) | A_BOLD);

    Canal *c = g->canal;
    const char *algo_str =
        c->cfg.flow_algo == FLOW_EQUIDAD ? "EQUIDAD" :
        c->cfg.flow_algo == FLOW_LETRERO ? "LETRERO" : "TICO";
    mvprintw(1, 2, "Flujo: %-8s | Canal: %d unidades | Cola max: %d",
             algo_str, c->cfg.canal_length, c->cfg.max_queue);

    mvprintw(2, 2, "Tipos: ");
    attron(COLOR_PAIR(COLOR_NORMAL_SHIP));  printw("[N]ormal  "); attroff(COLOR_PAIR(COLOR_NORMAL_SHIP));
    attron(COLOR_PAIR(COLOR_FISHING_SHIP)); printw("[F]Pesquero  "); attroff(COLOR_PAIR(COLOR_FISHING_SHIP));
    attron(COLOR_PAIR(COLOR_PATROL_SHIP));  printw("[P]atrulla"); attroff(COLOR_PAIR(COLOR_PATROL_SHIP));

    mvhline(3, 0, ACS_HLINE, g->cols);
}

void gui_draw_letrero(GuiState *g) {
    Canal *c = g->canal;
    pthread_mutex_lock(&c->lock);
    CanalDirection dir = c->current_dir;
    pthread_mutex_unlock(&c->lock);

    const char *letrero;
    int color;
    if      (dir == CANAL_DIR_LEFT)  { letrero = "[ IZQ --> DER ]"; color = COLOR_NORMAL_SHIP; }
    else if (dir == CANAL_DIR_RIGHT) { letrero = "[ DER <-- IZQ ]"; color = COLOR_PATROL_SHIP; }
    else                             { letrero = "[    LIBRE    ]"; color = COLOR_STATUS; }

    int col = (g->cols - (int)strlen(letrero)) / 2;
    attron(COLOR_PAIR(color) | A_BOLD);
    mvprintw(4, col, "%s", letrero);
    attroff(COLOR_PAIR(color) | A_BOLD);
}

static void print_ship_char(Ship *s) {
    int color;
    char sym;
    switch (s->type) {
        case SHIP_NORMAL:  color = COLOR_NORMAL_SHIP;  sym = 'N'; break;
        case SHIP_FISHING: color = COLOR_FISHING_SHIP; sym = 'F'; break;
        case SHIP_PATROL:  color = COLOR_PATROL_SHIP;  sym = 'P'; break;
        default:           color = COLOR_STATUS;        sym = '?'; break;
    }
    attron(COLOR_PAIR(color) | A_BOLD);
    printw("[%c%d]", sym, s->id);
    attroff(COLOR_PAIR(color) | A_BOLD);
}

void gui_draw_queues(GuiState *g) {
    Canal *c   = g->canal;
    int    row = GUI_CANAL_ROW_TOP - 2;

    pthread_mutex_lock(&c->queue_left.lock);
    mvprintw(row, 0, "Cola IZQ [%d]: ", c->queue_left.count);
    Ship *cur = c->queue_left.head;
    int off = 14;
    while (cur && off < g->cols / 3) {
        move(row, off); print_ship_char(cur);
        off += 5; cur = cur->next;
    }
    pthread_mutex_unlock(&c->queue_left.lock);

    pthread_mutex_lock(&c->queue_right.lock);
    int rstart = g->cols * 2 / 3;
    mvprintw(row, rstart, "Cola DER [%d]: ", c->queue_right.count);
    cur = c->queue_right.head;
    off = rstart + 14;
    while (cur && off < g->cols - 4) {
        move(row, off); print_ship_char(cur);
        off += 5; cur = cur->next;
    }
    pthread_mutex_unlock(&c->queue_right.lock);
}

void gui_draw_canal(GuiState *g) {
    Canal *c   = g->canal;
    int    len = c->cfg.canal_length;

    int cs = (g->cols - len * 3 - 4) / 2;
    if (cs < 2) cs = 2;
    int ce = cs + len * 3 + 2;

    int rt = GUI_CANAL_ROW_TOP;
    int rb = GUI_CANAL_ROW_BOT;

    attron(COLOR_PAIR(COLOR_CANAL_BG));
    mvprintw(rt-1, cs, "+"); for (int i=cs+1;i<ce;i++) mvaddch(rt-1,i,'-'); mvprintw(rt-1,ce,"+");
    mvprintw(rt,   cs, "|"); for (int i=cs+1;i<ce;i++) mvaddch(rt,  i,' '); mvprintw(rt,  ce,"|");
    mvprintw(rb,   cs, "|"); for (int i=cs+1;i<ce;i++) mvaddch(rb,  i,' '); mvprintw(rb,  ce,"|");
    mvprintw(rb+1, cs, "+"); for (int i=cs+1;i<ce;i++) mvaddch(rb+1,i,'-'); mvprintw(rb+1,ce,"+");
    attroff(COLOR_PAIR(COLOR_CANAL_BG));

    mvprintw(rt, cs-5, "-->");
    mvprintw(rb, ce+2, "<--");

    pthread_mutex_lock(&c->lock);
    for (int i = 0; i < c->in_canal_count; i++) {
        Ship *s = c->in_canal[i];
        if (!s || s->state != STATE_CROSSING) continue;

        int pos = s->position;
        int mx  = s->canal_length;
        int sc;

        if (s->direction == DIR_LEFT) {
            sc = cs + 1 + (pos * (len * 3)) / mx;
            if (sc > ce - 4) sc = ce - 4;
            move(rt, sc);
        } else {
            sc = ce - 1 - (pos * (len * 3)) / mx;
            if (sc < cs + 2) sc = cs + 2;
            move(rb, sc);
        }
        print_ship_char(s);
    }
    pthread_mutex_unlock(&c->lock);
}

void gui_draw_stats(GuiState *g) {
    Canal *c = g->canal;
    pthread_mutex_lock(&c->lock);
    int total = c->total_crossed, tl = c->total_left, tr = c->total_right;
    int in_now = c->in_canal_count, ql = c->queue_left.count, qr = c->queue_right.count;
    pthread_mutex_unlock(&c->lock);

    int row = GUI_STATUS_ROW;
    mvhline(row, 0, ACS_HLINE, g->cols);
    attron(COLOR_PAIR(COLOR_STATUS));
    mvprintw(row+1, 2,
        "Cruzaron: %d (L->R:%d  R->L:%d) | En canal: %d | Cola L:%d  R:%d",
        total, tl, tr, in_now, ql, qr);
    attroff(COLOR_PAIR(COLOR_STATUS));

    if (g->log_msg[0])
        mvprintw(row+2, 2, ">> %s", g->log_msg);
}

void gui_draw_help(GuiState *g) {
    int row = GUI_HELP_ROW;
    mvhline(row, 0, ACS_HLINE, g->cols);
    mvprintw(row+1, 2, "Teclas: [n] Normal IZQ  [N] Normal DER  [f] Pesquero IZQ  [F] Pesquero DER");
    mvprintw(row+2, 2, "        [p] Patrulla IZQ  [P] Patrulla DER  [w] Salir");
}

void gui_draw(GuiState *g) {
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

void *gui_render_thread(void *arg) {
    GuiState *g = (GuiState *)arg;
    while (g->running && g->canal->running) {
        gui_draw(g);
        usleep(50000);
    }
    return NULL;
}

int gui_handle_input(GuiState *g) {
    int ch = getch();
    if (ch == ERR) return 0;

    Canal *c = g->canal;
    switch (ch) {
        case 'w': case 'W':
            g->running = 0; c->running = 0;
            pthread_cond_broadcast(&c->can_enter);
            return 1;
        case 'n': gui_log(g, "Barco Normal L->R");
            ship_create(SHIP_NORMAL,  DIR_LEFT,  c->cfg.canal_length, 0); break;
        case 'N': gui_log(g, "Barco Normal R->L");
            ship_create(SHIP_NORMAL,  DIR_RIGHT, c->cfg.canal_length, 0); break;
        case 'f': gui_log(g, "Barco Pesquero L->R");
            ship_create(SHIP_FISHING, DIR_LEFT,  c->cfg.canal_length, 0); break;
        case 'F': gui_log(g, "Barco Pesquero R->L");
            ship_create(SHIP_FISHING, DIR_RIGHT, c->cfg.canal_length, 0); break;
        case 'p': gui_log(g, "Barco Patrulla L->R (URGENTE)");
            ship_create(SHIP_PATROL,  DIR_LEFT,  c->cfg.canal_length,
                        ship_default_burst(SHIP_PATROL, c->cfg.canal_length)); break;
        case 'P': gui_log(g, "Barco Patrulla R->L (URGENTE)");
            ship_create(SHIP_PATROL,  DIR_RIGHT, c->cfg.canal_length,
                        ship_default_burst(SHIP_PATROL, c->cfg.canal_length)); break;
        default: break;
    }
    return 0;
}