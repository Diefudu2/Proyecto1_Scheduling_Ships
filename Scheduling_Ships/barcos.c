#include <stdio.h>
#include "barcos.h"

int barco_velocidad(int tipo) {
    switch (tipo) {
        case TIPO_NORMAL:   return VEL_NORMAL;
        case TIPO_PESQUERO: return VEL_PESQUERO;
        case TIPO_PATRULLA: return VEL_PATRULLA;
        default:            return VEL_NORMAL;
    }
}

Barco barco_crear(int id, int tipo, int lado, int prioridad,
                  int tiempo, int deadline, int tick_llegada) {
    Barco b;

    b.id           = id;
    b.tipo         = tipo;
    b.lado         = lado;
    b.estado       = ESTADO_LISTO;
    b.posicion     = -1;          // -1 = aún no está en el canal
    b.velocidad    = barco_velocidad(tipo);

    b.prioridad    = prioridad;
    b.tiempo       = tiempo;
    b.tiempo_rest  = tiempo;
    b.deadline     = deadline;
    b.quantum_rest = 0;           // se asigna al entrar al canal con RR
    b.tick_llegada = tick_llegada;

    return b;
}

void barco_imprimir(Barco* b) {
    // Nombre del tipo
    const char* nombre_tipo;
    switch (b->tipo) {
        case TIPO_NORMAL:   nombre_tipo = "Normal";   break;
        case TIPO_PESQUERO: nombre_tipo = "Pesquero"; break;
        case TIPO_PATRULLA: nombre_tipo = "Patrulla"; break;
        default:            nombre_tipo = "Desconocido";
    }

    // Nombre del lado
    const char* nombre_lado = (b->lado == LADO_IZQUIERDA) ? "Izquierda" : "Derecha";

    // Nombre del estado
    const char* nombre_estado;
    switch (b->estado) {
        case ESTADO_LISTO:     nombre_estado = "Listo";     break;
        case ESTADO_EN_CANAL:  nombre_estado = "En canal";  break;
        case ESTADO_TERMINADO: nombre_estado = "Terminado"; break;
        case ESTADO_BLOQUEADO: nombre_estado = "Bloqueado"; break;
        default:               nombre_estado = "Desconocido";
    }

    printf("Barco #%d | Tipo: %-9s | Lado: %-10s | Estado: %-10s\n",
           b->id, nombre_tipo, nombre_lado, nombre_estado);
    printf("         | Pos: %2d | Vel: %d | Prior: %2d | Tiempo: %2d/%2d"
           " | Deadline: %2d | Llegada: %d\n",
           b->posicion, b->velocidad, b->prioridad,
           b->tiempo - b->tiempo_rest, b->tiempo,
           b->deadline, b->tick_llegada);
}