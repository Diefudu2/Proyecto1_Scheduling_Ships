#ifndef CALENDARIZADOR_H
#define CALENDARIZADOR_H

#include "barcos.h"

#define MAX_POR_LADO 4

// ─── Round Robin ──────────────────────────────────────────────────────────────
// Ejecuta RR sobre una cola de barcos.
// quantum: número de ticks que tiene cada barco antes de rotar.
// Imprime el orden de ejecución tick a tick.
void rr(Barco* cola, int total, int quantum);

#endif // CALENDARIZADOR_H