#include <stdio.h>
#include "calendarizador.h"

// ─── Round Robin ──────────────────────────────────────────────────────────────
// Recibe la cola de un lado y el quantum.
// Cada barco avanza quantum ticks, luego rota al siguiente.
// Si un barco termina antes del quantum, pasa al siguiente sin esperar.
// Se repite hasta que todos los barcos hayan terminado.
void rr(Barco* cola, int total, int quantum) {
    if (total == 0) {
        printf("Cola vacía, nada que calendarizar.\n");
        return;
    }

    // Inicializar tiempo restante de cada barco
    for (int i = 0; i < total; i++) {
        cola[i].tiempo_rest  = cola[i].tiempo;
        cola[i].quantum_rest = quantum;
        cola[i].estado       = ESTADO_LISTO;
    }

    int tick         = 0;
    int terminados   = 0;
    int idx          = 0;   // índice del barco actual en la cola

    printf("\n─── Round Robin (quantum=%d) ───────────────────────────\n",
           quantum);
    printf("%-6s %-10s %-12s %-12s %-10s\n",
           "Tick", "Barco ID", "Tipo", "T.Rest", "Estado");
    printf("────────────────────────────────────────────────────────\n");

    while (terminados < total) {
        // Buscar el siguiente barco listo (saltar terminados)
        int intentos = 0;
        while (cola[idx].estado == ESTADO_TERMINADO) {
            idx = (idx + 1) % total;
            intentos++;
            if (intentos > total) break; // todos terminados
        }

        Barco* b = &cola[idx];
        if (b->estado == ESTADO_TERMINADO) break;

        b->estado       = ESTADO_EN_CANAL;
        b->quantum_rest = quantum;

        // El barco ejecuta hasta agotar quantum o terminar
        while (b->quantum_rest > 0 && b->tiempo_rest > 0) {
            const char* nombre_tipo;
            switch (b->tipo) {
                case TIPO_NORMAL:   nombre_tipo = "Normal";   break;
                case TIPO_PESQUERO: nombre_tipo = "Pesquero"; break;
                case TIPO_PATRULLA: nombre_tipo = "Patrulla"; break;
                default:            nombre_tipo = "?";
            }
            printf("%-6d %-10d %-12s %-12d %-10s\n",
                   tick, b->id, nombre_tipo, b->tiempo_rest, "En canal");

            b->tiempo_rest--;
            b->quantum_rest--;
            tick++;
        }

        if (b->tiempo_rest <= 0) {
            b->estado = ESTADO_TERMINADO;
            terminados++;
            printf("       >> Barco #%d terminó en tick %d\n", b->id, tick);
        } else {
            b->estado = ESTADO_LISTO;
            printf("       >> Barco #%d agotó quantum, regresa a cola\n", b->id);
        }

        // Rotar al siguiente
        idx = (idx + 1) % total;
    }

    printf("────────────────────────────────────────────────────────\n");
    printf("Total ticks: %d | Barcos completados: %d\n", tick, terminados);
}