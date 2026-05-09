#include "uthread.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

/* =========================================================
 * uthread.c — Implementación de hilos en espacio de usuario
 *
 * Usa la API POSIX ucontext_t (getcontext / makecontext /
 * swapcontext) para crear y cambiar contextos de ejecución
 * sin intervención del kernel.
 *
 * Restricción del proyecto: el kernel solo ve UN hilo real.
 * Todos los "barcos" son UThreads que comparten ese hilo.
 * ========================================================= */


/* ---------------------------------------------------------
 * Globales
 * --------------------------------------------------------- */

/* Contexto del scheduler: el "home base" al que vuelven
 * todos los UThreads cuando hacen yield o exit.
 * Definido acá, declarado extern en uthread.h             */
ucontext_t  g_sched_ctx;

/* Hilo que está corriendo en este momento.
 * NULL cuando está corriendo el scheduler.               */
UThread    *g_current = NULL;

/* Contador para asignar IDs únicos                       */
static int  g_next_id = 1;


/* ---------------------------------------------------------
 * Trampoline — función que arranca cada UThread
 *
 * makecontext() solo acepta argumentos de tipo int.
 * En sistemas de 64 bits un puntero ocupa 64 bits, así que
 * lo partimos en dos ints (hi y lo) y lo reconstruimos acá.
 *
 * Flujo:
 *   1. Reconstruye el puntero al UThread
 *   2. Llama a t->func(t->arg)  ← el barco corre acá
 *   3. Cuando func() retorna, llama uthread_exit()
 *      (por si el barco no lo llamó explícitamente)
 * --------------------------------------------------------- */
static void uthread_trampoline(unsigned int hi, unsigned int lo)
{
    /* Reconstruir el puntero desde dos ints de 32 bits */
    uintptr_t ptr = ((uintptr_t)hi << 32) | (uintptr_t)lo;
    UThread  *t   = (UThread *)ptr;

    /* Ejecutar la función del barco */
    t->func(t->arg);

    /* Si el barco no llamó uthread_exit() explícitamente,
     * lo hacemos acá para no dejar el contexto colgado    */
    uthread_exit();
}


/* ---------------------------------------------------------
 * uthread_create
 * --------------------------------------------------------- */
UThread *uthread_create(void (*func)(void *arg), void *arg,
                        size_t stack_size)
{
    if (stack_size == 0)
        stack_size = UTHREAD_STACK_SIZE;

    /* Alocar la estructura */
    UThread *t = calloc(1, sizeof(UThread));
    if (!t) {
        perror("uthread_create: calloc UThread");
        return NULL;
    }

    /* Alocar el stack */
    t->stack = malloc(stack_size);
    if (!t->stack) {
        perror("uthread_create: malloc stack");
        free(t);
        return NULL;
    }

    t->stack_size = stack_size;
    t->func       = func;
    t->arg        = arg;
    t->state      = UTHREAD_READY;
    t->id         = g_next_id++;
    t->next       = NULL;

    /* Inicializar el contexto POSIX */
    if (getcontext(&t->ctx) == -1) {
        perror("uthread_create: getcontext");
        free(t->stack);
        free(t);
        return NULL;
    }

    /* Configurar el stack para este contexto */
    t->ctx.uc_stack.ss_sp   = t->stack;
    t->ctx.uc_stack.ss_size = stack_size;

    /* Cuando este contexto termine, volver al scheduler.
     * uc_link apunta al contexto que se activa al retornar;
     * en nuestro caso es g_sched_ctx (el loop del scheduler).
     *
     * IMPORTANTE: makecontext() no usa uc_link si el hilo
     * termina via swapcontext() — solo si retorna normalmente
     * del trampoline. Aún así es buena práctica setearlo.   */
    t->ctx.uc_link = &g_sched_ctx;

    /* Partir el puntero en dos ints para makecontext()
     * (necesario en arquitecturas de 64 bits)               */
    uintptr_t ptr = (uintptr_t)t;
    unsigned int hi = (unsigned int)(ptr >> 32);
    unsigned int lo = (unsigned int)(ptr & 0xFFFFFFFF);

    /* Registrar la función de arranque con sus argumentos */
    makecontext(&t->ctx,
                (void (*)(void))uthread_trampoline,
                2, hi, lo);

    return t;
}


/* ---------------------------------------------------------
 * uthread_yield
 *
 * El hilo actual cede el control al scheduler.
 * swapcontext guarda el estado actual en g_current->ctx
 * y restaura g_sched_ctx (el loop del scheduler).
 *
 * Cuando el scheduler vuelva a elegir este hilo,
 * swapcontext retornará acá y la ejecución continúa.
 * --------------------------------------------------------- */
void uthread_yield(void)
{
    assert(g_current != NULL &&
           "uthread_yield() llamado fuera de un UThread");
    assert(g_current->state == UTHREAD_RUNNING);

    /* Pasar a READY para que el scheduler pueda re-elegirlo */
    g_current->state = UTHREAD_READY;

    /* Guardar contexto actual y saltar al scheduler         */
    swapcontext(&g_current->ctx, &g_sched_ctx);

    /* --- el scheduler vuelve a activar este hilo acá ---   */
    /* Cuando llegamos acá, el scheduler ya puso
     * g_current = este hilo y state = RUNNING              */
}


/* ---------------------------------------------------------
 * uthread_exit
 *
 * El hilo actual termina su ejecución.
 * Marca estado DONE y cede al scheduler definitivamente.
 * El scheduler nunca volverá a elegir este hilo.
 * --------------------------------------------------------- */
void uthread_exit(void)
{
    assert(g_current != NULL &&
           "uthread_exit() llamado fuera de un UThread");

    g_current->state = UTHREAD_DONE;

    /* Saltar al scheduler sin guardar el contexto actual
     * (este contexto no se va a reanudar nunca más).
     *
     * Usamos swapcontext igualmente para no perder el frame
     * actual en el stack — usar setcontext() directamente
     * podría corromper el stack en algunos compiladores.    */
    swapcontext(&g_current->ctx, &g_sched_ctx);

    /* No debería llegar acá nunca */
    assert(0 && "uthread_exit() retornó — esto no debe pasar");
}


/* ---------------------------------------------------------
 * uthread_destroy
 *
 * Libera stack y estructura. Solo llamar cuando DONE.
 * --------------------------------------------------------- */
void uthread_destroy(UThread *t)
{
    if (!t) return;

    assert(t->state == UTHREAD_DONE &&
           "uthread_destroy() sobre hilo no terminado");

    free(t->stack);
    t->stack = NULL;
    free(t);
}