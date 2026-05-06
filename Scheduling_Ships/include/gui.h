#ifndef GUI_H
#define GUI_H

#include "canal.h"
#include "ship.h"

#define COLOR_NORMAL_SHIP   1
#define COLOR_FISHING_SHIP  2
#define COLOR_PATROL_SHIP   3
#define COLOR_CANAL_BG      4
#define COLOR_QUEUE_BG      5
#define COLOR_HEADER        6
#define COLOR_STATUS        7

#define GUI_CANAL_ROW_TOP    6
#define GUI_CANAL_ROW_BOT    9
#define GUI_QUEUE_ROWS       4
#define GUI_STATUS_ROW      14
#define GUI_HELP_ROW        20

typedef struct {
    Canal   *canal;
    int      running;
    int      cols;
    int      rows;
    char     log_msg[256];
} GuiState;

void  gui_init          (GuiState *g, Canal *c);
void  gui_destroy       (GuiState *g);
void  gui_draw          (GuiState *g);
void  gui_draw_header   (GuiState *g);
void  gui_draw_canal    (GuiState *g);
void  gui_draw_queues   (GuiState *g);
void  gui_draw_letrero  (GuiState *g);
void  gui_draw_stats    (GuiState *g);
void  gui_draw_help     (GuiState *g);
void  gui_log           (GuiState *g, const char *fmt, ...);
void *gui_render_thread (void *arg);
int   gui_handle_input  (GuiState *g);

#endif 