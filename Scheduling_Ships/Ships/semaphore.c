/* ============================================================
 * Archivo: semaphore.c
 * Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
 * Rol: Implementa semáforos simulados para proteger cupos del canal y posiciones lógicas.
 *
 * Documentación interna:
 * - Mantener este módulo pequeño, con validaciones defensivas y sin asumir entradas válidas.
 *
 * Convenciones:
 * - Las funciones públicas se declaran en el .h correspondiente.
 * - Las funciones static son utilidades internas del archivo.
 * - Retornos int usan 1=éxito/verdadero y 0=fallo/falso salvo que se indique otra cosa.
 * ============================================================ */
#include "semaphore.h"

#include <stddef.h>

void sim_sem_init(SimSemaphore *sem, int initial_value)
{
    if (!sem) {
        return;
    }

    sem->value = initial_value;
    sem->blocked_head = NULL;
    sem->blocked_tail = NULL;
}

static void sim_sem_block_thread(SimSemaphore *sem, SimThread *thread)
{
    if (!sem || !thread) {
        return;
    }

    thread_block(thread);
    thread->next = NULL;

    if (sem->blocked_tail) {
        sem->blocked_tail->next = thread;
    } else {
        sem->blocked_head = thread;
    }

    sem->blocked_tail = thread;
}

int sim_sem_wait(SimSemaphore *sem, SimThread *thread)
{
    if (!sem || !thread) {
        return 0;
    }

    if (sem->value > 0) {
        sem->value--;
        return 1;
    }

    sim_sem_block_thread(sem, thread);
    return 0;
}

SimThread *sim_sem_signal(SimSemaphore *sem)
{
    if (!sem) {
        return NULL;
    }

    if (sem->blocked_head) {
        SimThread *thread = sem->blocked_head;

        sem->blocked_head = thread->next;

        if (!sem->blocked_head) {
            sem->blocked_tail = NULL;
        }

        thread->next = NULL;
        thread_set_ready(thread);

        return thread;
    }

    sem->value++;
    return NULL;
}

int sim_sem_value(SimSemaphore *sem)
{
    if (!sem) {
        return 0;
    }

    return sem->value;
}