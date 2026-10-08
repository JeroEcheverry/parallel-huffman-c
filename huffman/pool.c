/* Reparte tareas entre los hilos trabajadores. */
#include "huffman.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Cantidad de hilos que se usara por defecto. */
int huff_hilos_por_defecto(void)
{
    long nucleos = sysconf(_SC_NPROCESSORS_ONLN);
    if (nucleos < 1) return 1;
    if (nucleos > HUFF_MAX_HILOS) return HUFF_MAX_HILOS;
    return (int)nucleos;
}

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

/* Cada hilo toma tareas hasta que no queden o ocurra un error. */
static void *trabajador(void *arg)
{
    Pool    *pool = arg;
    uint64_t indice;

    while (tomar_tarea(pool, &indice)) {
        if (pool->demora_ms > 0) {
            usleep((useconds_t)pool->demora_ms * 1000);
        }

        if (pool->tarea(pool->contexto, indice) != 0) {
            pthread_mutex_lock(&pool->mutex);
            pool->error = 1;
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
    if ((uint64_t)n_hilos > n_tareas) n_hilos = (int)n_tareas;

    pool->n_hilos   = 0;
    pool->siguiente = 0;
    pool->n_tareas  = n_tareas;
    pool->error     = 0;
    pool->tarea     = tarea;
    pool->contexto  = contexto;
    pool->hilos     = NULL;

    const char *demora = getenv("HUFF_DEMORA_MS");
    pool->demora_ms = (demora != NULL) ? atoi(demora) : 0;

    pthread_mutex_init(&pool->mutex, NULL);

    if (n_hilos == 0) {
        return 0;
    }

    pool->hilos = malloc((size_t)n_hilos * sizeof(pthread_t));
    if (pool->hilos == NULL) {
        pthread_mutex_destroy(&pool->mutex);
        return -1;
    }

    for (int i = 0; i < n_hilos; i++) {
        if (pthread_create(&pool->hilos[i], NULL, trabajador, pool) != 0) {
            fprintf(stderr, "Error: no se pudo crear el hilo %d\n", i);
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
    for (int i = 0; i < pool->n_hilos; i++) {
        pthread_join(pool->hilos[i], NULL);
    }

    int error = pool->error;

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
