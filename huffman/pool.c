/*
 * pool.c -- pool de hilos trabajadores con reparto dinamico de tareas.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Las tareas estan numeradas de 0 a n_tareas-1 (por ejemplo, un numero
 * por bloque). Todos los hilos comparten el contador 'siguiente':
 *
 *     hilo libre -> bloquea mutex -> toma 'siguiente' y lo incrementa
 *                -> libera mutex  -> procesa esa tarea -> repite
 *
 * El mutex solo protege la asignacion (unas pocas instrucciones). El
 * trabajo pesado se hace fuera de la seccion critica, asi los hilos
 * trabajan en paralelo y casi nunca esperan el mutex.
 */
#include "huffman.h"

#include <stdio.h>     /* fprintf                        */
#include <stdlib.h>    /* malloc, free, getenv, atoi     */
#include <unistd.h>    /* sysconf, usleep                */

int huff_hilos_por_defecto(void)
{
    long nucleos = sysconf(_SC_NPROCESSORS_ONLN);
    if (nucleos < 1) return 1;
    if (nucleos > HUFF_MAX_HILOS) return HUFF_MAX_HILOS;
    return (int)nucleos;
}

/*
 * Toma la siguiente tarea libre. Retorna 1 y deja el numero en *indice,
 * o 0 si ya no quedan tareas o si alguna tarea fallo.
 */
static int tomar_tarea(Pool *pool, uint64_t *indice)
{
    int hay = 0;

    pthread_mutex_lock(&pool->mutex);
    if (!pool->error && pool->siguiente < pool->n_tareas) {
        *indice = pool->siguiente;
        pool->siguiente++;
        hay = 1;
    }
    pthread_mutex_unlock(&pool->mutex);

    return hay;
}

/* Funcion que ejecuta cada hilo del pool. */
static void *trabajador(void *arg)
{
    Pool    *pool = arg;
    uint64_t indice;

    while (tomar_tarea(pool, &indice)) {
        /* Pausa opcional para que en una demostracion se vea el avance
           con archivos pequenos. No es espera activa: el hilo duerme. */
        if (pool->demora_ms > 0) {
            usleep((useconds_t)pool->demora_ms * 1000);
        }

        if (pool->tarea(pool->contexto, indice) != 0) {
            pthread_mutex_lock(&pool->mutex);
            pool->error = 1;          /* los demas dejan de tomar tareas */
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
    }
    return NULL;
}

int pool_iniciar(Pool *pool, int n_hilos, uint64_t n_tareas,
                 PoolTarea tarea, void *contexto)
{
    if (n_hilos <= 0)            n_hilos = huff_hilos_por_defecto();
    if (n_hilos > HUFF_MAX_HILOS) n_hilos = HUFF_MAX_HILOS;
    /* No tiene sentido crear mas hilos que tareas. */
    if ((uint64_t)n_hilos > n_tareas) n_hilos = (int)n_tareas;

    pool->n_hilos   = 0;
    pool->siguiente = 0;
    pool->n_tareas  = n_tareas;
    pool->error     = 0;
    pool->tarea     = tarea;
    pool->contexto  = contexto;
    pool->hilos     = NULL;

    /* HUFF_DEMORA_MS: variable de entorno solo para demostraciones. */
    const char *demora = getenv("HUFF_DEMORA_MS");
    pool->demora_ms = (demora != NULL) ? atoi(demora) : 0;

    pthread_mutex_init(&pool->mutex, NULL);

    if (n_hilos == 0) {
        return 0;   /* no hay tareas: no se crea ningun hilo */
    }

    pool->hilos = malloc((size_t)n_hilos * sizeof(pthread_t));
    if (pool->hilos == NULL) {
        pthread_mutex_destroy(&pool->mutex);
        return -1;
    }

    for (int i = 0; i < n_hilos; i++) {
        if (pthread_create(&pool->hilos[i], NULL, trabajador, pool) != 0) {
            fprintf(stderr, "Error: no se pudo crear el hilo %d\n", i);
            /* Se detienen los hilos ya creados y se espera a que salgan. */
            pthread_mutex_lock(&pool->mutex);
            pool->error = 1;
            pthread_mutex_unlock(&pool->mutex);
            pool_esperar(pool);
            return -1;
        }
        pool->n_hilos++;
    }
    return 0;
}

int pool_esperar(Pool *pool)
{
    /* join: el hilo que llama duerme hasta que cada trabajador termine. */
    for (int i = 0; i < pool->n_hilos; i++) {
        pthread_join(pool->hilos[i], NULL);
    }

    int error = pool->error;   /* ya no hay otros hilos: se lee sin mutex */

    free(pool->hilos);
    pool->hilos   = NULL;
    pool->n_hilos = 0;
    pthread_mutex_destroy(&pool->mutex);

    return error ? -1 : 0;
}

int pool_ejecutar(int n_hilos, uint64_t n_tareas, PoolTarea tarea, void *contexto)
{
    Pool pool;
    if (pool_iniciar(&pool, n_hilos, n_tareas, tarea, contexto) < 0) {
        return -1;
    }
    return pool_esperar(&pool);
}
