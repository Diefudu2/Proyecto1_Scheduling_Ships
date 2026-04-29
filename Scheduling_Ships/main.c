#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "barcos.h"
#include "calendarizador.h"

#define MAX_POR_LADO 4
#define QUANTUM_DEFAULT 3

Barco cola_izq[MAX_POR_LADO];
Barco cola_der[MAX_POR_LADO];
int total_izq   = 0;
int total_der   = 0;
int id_contador = 1;

char leer_tecla() {
    struct termios old, new;
    tcgetattr(STDIN_FILENO, &old);
    new = old;
    new.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new);
    char c = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    return c;
}

void agregar_barco(int tipo, int lado) {
    int*   total      = (lado == LADO_IZQUIERDA) ? &total_izq : &total_der;
    Barco* cola       = (lado == LADO_IZQUIERDA) ? cola_izq   : cola_der;
    const char* nombre = (lado == LADO_IZQUIERDA) ? "Izquierda" : "Derecha";

    if (*total >= MAX_POR_LADO) {
        printf("Cola %s llena (máximo %d).\n", nombre, MAX_POR_LADO);
        return;
    }
    int tick_llegada = total_izq + total_der;
    cola[*total] = barco_crear(id_contador++, tipo, lado,
                               5, 10, 30, tick_llegada);
    printf("\n>>> Barco creado:\n");
    barco_imprimir(&cola[*total]);
    (*total)++;
}

void cargar_config(const char* archivo) {
    FILE* f = fopen(archivo, "r");
    if (!f) {
        printf("No se encontró '%s', iniciando sin carga previa.\n", archivo);
        return;
    }
    int lado, tipo;
    while (fscanf(f, "%d %d", &lado, &tipo) == 2) {
        if (lado < 0 || lado > 1 || tipo < 1 || tipo > 3) {
            printf("Línea inválida (lado=%d tipo=%d), ignorada.\n", lado, tipo);
            continue;
        }
        agregar_barco(tipo, lado);
    }
    fclose(f);
    printf("\nCarga previa: %d izquierda, %d derecha.\n", total_izq, total_der);
}

void mostrar_cola() {
    printf("\n─── Cola Izquierda (%d/%d) ───────────────────────────────\n",
           total_izq, MAX_POR_LADO);
    if (total_izq == 0) printf("  Vacía\n");
    for (int i = 0; i < total_izq; i++) {
        barco_imprimir(&cola_izq[i]);
        printf("\n");
    }
    printf("─── Cola Derecha (%d/%d) ─────────────────────────────────\n",
           total_der, MAX_POR_LADO);
    if (total_der == 0) printf("  Vacía\n");
    for (int i = 0; i < total_der; i++) {
        barco_imprimir(&cola_der[i]);
        printf("\n");
    }
}

void menu_teclado() {
    printf("\n─── Controles ──────────────────────────────────────────\n");
    printf("  I = generar barco Izquierda   D = generar barco Derecha\n");
    printf("  1 = Normal   2 = Pesquero     3 = Patrulla\n");
    printf("  W = ver colas\n");
    printf("  R = ejecutar Round Robin\n");
    printf("  Q = salir\n");
    printf("────────────────────────────────────────────────────────\n");
}

int main(int argc, char* argv[]) {
    printf("=== Scheduling Ships ===\n\n");

    if (argc >= 2) {
        printf("Cargando configuración desde: %s\n", argv[1]);
        cargar_config(argv[1]);
    } else {
        printf("Uso: %s [archivo_config]\n", argv[0]);
        printf("Iniciando sin carga previa...\n");
    }

    menu_teclado();

    int corriendo = 1;
    while (corriendo) {
        printf("\nEsperando tecla: ");
        fflush(stdout);
        char c = leer_tecla();
        printf("%c\n", c);

        int lado = -1;
        switch (c) {
            case 'i': case 'I': lado = LADO_IZQUIERDA; break;
            case 'd': case 'D': lado = LADO_DERECHA;   break;

            case 'w': case 'W':
                mostrar_cola();
                continue;

            case 'r': case 'R':
                printf("RR sobre qué cola? (I=Izquierda D=Derecha): ");
                fflush(stdout);
                char lado_rr = leer_tecla();
                printf("%c\n", lado_rr);
                if (lado_rr == 'i' || lado_rr == 'I')
                    rr(cola_izq, total_izq, QUANTUM_DEFAULT);
                else if (lado_rr == 'd' || lado_rr == 'D')
                    rr(cola_der, total_der, QUANTUM_DEFAULT);
                else
                    printf("Opción inválida.\n");
                continue;

            case 'q': case 'Q':
                printf("Saliendo...\n");
                corriendo = 0;
                continue;

            default:
                printf("Tecla no reconocida.\n");
                continue;
        }

        printf("Tipo (1=Normal 2=Pesquero 3=Patrulla): ");
        fflush(stdout);
        char t = leer_tecla();
        printf("%c\n", t);
        if (t >= '1' && t <= '3')
            agregar_barco(t - '0', lado);
        else
            printf("Tipo inválido.\n");
    }

    return 0;
}