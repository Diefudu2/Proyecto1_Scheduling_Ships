/*
 * Parche recomendado para serial_protocol.c
 *
 * Objetivo:
 *   Evitar pedir STATUS + CANAL + THREADS + FLOW constantemente.
 *   En su lugar, la GUI pide SNAPSHOT y recibe una sola línea compacta.
 *
 * Formato:
 *   SNAPSHOT SCHED=PRIORITY FLOW=LETRERO RUN=1 LEN=20 COUNT=2 LQ=1:PATROL:L,2:NORMAL:L RQ=5:PATROL:R C=3:1:PATROL:L:1800:1,8:2:NORMAL:L:5000:4
 *
 * Campos:
 *   LQ = ready queue izquierda: id:type:dir
 *   RQ = ready queue derecha: id:type:dir
 *   C  = canal: pos:id:type:dir:remaining:led_slot
 */

/* 1. Agregue este prototipo arriba de serial_protocol.c */
static void serial_protocol_send_snapshot(void);

/* 2. Agregue este bloque dentro de serial_protocol_handle_command(): */

/*
else if (strcmp(cmd, "SNAPSHOT") == 0) {
    serial_protocol_send_snapshot();
}
*/

/* 3. Agregue esta función en serial_protocol.c */

static void append_text(char *buffer, int buffer_size, int *used, const char *text)
{
    if (!buffer || !used || !text || buffer_size <= 0) {
        return;
    }

    if (*used >= buffer_size) {
        return;
    }

    int written = snprintf(buffer + *used, buffer_size - *used, "%s", text);

    if (written < 0) {
        return;
    }

    *used += written;

    if (*used >= buffer_size) {
        *used = buffer_size - 1;
        buffer[*used] = '\0';
    }
}

static void serial_protocol_send_snapshot(void)
{
    char line[900];
    char item[96];
    int used = 0;

    SystemConfig *cfg = config_get();

    used += snprintf(line,
                     sizeof(line),
                     "SNAPSHOT SCHED=%s FLOW=%s RUN=%d LEN=%d COUNT=%d ",
                     scheduler_algo_name(scheduler_get_algorithm()),
                     canal_flow_name(cfg->flow_algo),
                     scheduler_is_enabled(),
                     canal_get_length(),
                     canal_get_ship_count());

    /*
     * READY izquierda y derecha.
     */
    append_text(line, sizeof(line), &used, "LQ=");

    int first = 1;
    SimThread *cur = scheduler_get_ready_head();

    while (cur) {
        Ship *s = (Ship *)cur->arg;

        if (s && cur->state == THREAD_READY && s->dir == DIR_LEFT_TO_RIGHT) {
            if (!first) {
                append_text(line, sizeof(line), &used, ",");
            }

            snprintf(item,
                     sizeof(item),
                     "%d:%s:%s",
                     s->id,
                     ship_type_name(s->type),
                     ship_dir_name(s->dir));
            append_text(line, sizeof(line), &used, item);
            first = 0;
        }

        cur = cur->next;
    }

    if (first) {
        append_text(line, sizeof(line), &used, "-");
    }

    append_text(line, sizeof(line), &used, " RQ=");

    first = 1;
    cur = scheduler_get_ready_head();

    while (cur) {
        Ship *s = (Ship *)cur->arg;

        if (s && cur->state == THREAD_READY && s->dir == DIR_RIGHT_TO_LEFT) {
            if (!first) {
                append_text(line, sizeof(line), &used, ",");
            }

            snprintf(item,
                     sizeof(item),
                     "%d:%s:%s",
                     s->id,
                     ship_type_name(s->type),
                     ship_dir_name(s->dir));
            append_text(line, sizeof(line), &used, item);
            first = 0;
        }

        cur = cur->next;
    }

    if (first) {
        append_text(line, sizeof(line), &used, "-");
    }

    /*
     * Canal.
     */
    append_text(line, sizeof(line), &used, " C=");

    first = 1;

    int len = canal_get_length();

    for (int pos = 0; pos < len; pos++) {
        Ship *s = canal_get_ship_at_position(pos);

        if (!s) {
            continue;
        }

        if (!first) {
            append_text(line, sizeof(line), &used, ",");
        }

        snprintf(item,
                 sizeof(item),
                 "%d:%d:%s:%s:%d:%d",
                 pos,
                 s->id,
                 ship_type_name(s->type),
                 ship_dir_name(s->dir),
                 s->remaining_ms,
                 canal_position_to_led_slot(pos));

        append_text(line, sizeof(line), &used, item);
        first = 0;
    }

    if (first) {
        append_text(line, sizeof(line), &used, "-");
    }

    serial_protocol_send_line(line);
}
