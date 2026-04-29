#ifndef BARCO_H
#define BARCO_H

// ─── Tipos de barco ───────────────────────────────────────────────────────────
#define TIPO_NORMAL   1
#define TIPO_PESQUERO 2
#define TIPO_PATRULLA 3

// ─── Lado del canal ───────────────────────────────────────────────────────────
#define LADO_IZQUIERDA 0
#define LADO_DERECHA   1

// ─── Estados del barco ────────────────────────────────────────────────────────
#define ESTADO_LISTO      0  // en cola, esperando
#define ESTADO_EN_CANAL   1  // transitando el canal
#define ESTADO_TERMINADO  2  // llegó al otro lado
#define ESTADO_BLOQUEADO  3  // sacado por interrupción, reingresa a cola

// ─── Velocidades base por tipo (LEDs por tick) ────────────────────────────────
#define VEL_NORMAL    1
#define VEL_PESQUERO  2
#define VEL_PATRULLA  3

// ─── Estructura principal ─────────────────────────────────────────────────────
typedef struct {
    int id;            // identificador único del barco
    int tipo;          // TIPO_NORMAL, TIPO_PESQUERO, TIPO_PATRULLA
    int lado;          // LADO_IZQUIERDA o LADO_DERECHA
    int estado;        // ESTADO_LISTO, EN_CANAL, TERMINADO, BLOQUEADO
    int posicion;      // posición actual en la tira (índice LED)
    int velocidad;     // LEDs por tick según tipo

    // ── Atributos para calendarizadores ──
    int prioridad;     // para algoritmo Prioridad (menor = mayor prioridad)
    int tiempo;        // burst time total (SJF, STRN)
    int tiempo_rest;   // tiempo restante en canal (STRN, RR)
    int deadline;      // tiempo máximo para cruzar el canal (EDF)
    int quantum_rest;  // quantum restante en el canal (RR)
    int tick_llegada;  // tick en que el barco entró a la cola (FCFS)
} Barco;

// ─── Funciones ────────────────────────────────────────────────────────────────

// Crea un barco con los parámetros dados y valores por defecto
Barco barco_crear(int id, int tipo, int lado, int prioridad,
                  int tiempo, int deadline, int tick_llegada);

// Retorna la velocidad correspondiente al tipo de barco
int barco_velocidad(int tipo);

// Imprime los atributos del barco (para debug)
void barco_imprimir(Barco* b);

#endif // BARCO_H