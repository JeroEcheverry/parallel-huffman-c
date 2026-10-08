/* Guarda el avance de una tarea para que otros hilos puedan consultarlo. */
#include "huffman.h"

#include <time.h>

/* Inicializacion, cierre y actualizacion del avance. */
void huff_progreso_iniciar(HuffProgreso *p)
{
    if (p == NULL) return;
    pthread_mutex_init(&p->mutex, NULL);
    pthread_cond_init(&p->cambio, NULL);
    p->hechos    = 0;
    p->total     = 0;
    p->terminado = 0;
    p->cancelar  = 0;
}

void huff_progreso_destruir(HuffProgreso *p)
{
    if (p == NULL) return;
    pthread_cond_destroy(&p->cambio);
    pthread_mutex_destroy(&p->mutex);
}

void huff_progreso_fijar_total(HuffProgreso *p, uint64_t total)
{
    if (p == NULL) return;
    pthread_mutex_lock(&p->mutex);
    p->hechos = 0;
    p->total  = total;
    pthread_cond_broadcast(&p->cambio);
    pthread_mutex_unlock(&p->mutex);
}

void huff_progreso_avanzar(HuffProgreso *p, uint64_t n)
{
    if (p == NULL) return;
    pthread_mutex_lock(&p->mutex);
    p->hechos += n;
    pthread_cond_broadcast(&p->cambio);
    pthread_mutex_unlock(&p->mutex);
}

void huff_progreso_terminar(HuffProgreso *p)
{
    if (p == NULL) return;
    pthread_mutex_lock(&p->mutex);
    p->terminado = 1;
    pthread_cond_broadcast(&p->cambio);
    pthread_mutex_unlock(&p->mutex);
}

void huff_progreso_cancelar(HuffProgreso *p)
{
    if (p == NULL) return;
    pthread_mutex_lock(&p->mutex);
    p->cancelar = 1;
    pthread_cond_broadcast(&p->cambio);
    pthread_mutex_unlock(&p->mutex);
}

int huff_progreso_cancelado(HuffProgreso *p)
{
    if (p == NULL) return 0;
    pthread_mutex_lock(&p->mutex);
    int c = p->cancelar;
    pthread_mutex_unlock(&p->mutex);
    return c;
}

void huff_progreso_leer(HuffProgreso *p, uint64_t *hechos, uint64_t *total,
                        int *terminado)
{
    if (p == NULL) return;
    pthread_mutex_lock(&p->mutex);
    if (hechos    != NULL) *hechos    = p->hechos;
    if (total     != NULL) *total     = p->total;
    if (terminado != NULL) *terminado = p->terminado;
    pthread_mutex_unlock(&p->mutex);
}

int huff_progreso_esperar(HuffProgreso *p, int espera_ms)
{
    if (p == NULL) return 1;

    struct timespec limite;
    clock_gettime(CLOCK_REALTIME, &limite);
    limite.tv_sec  += espera_ms / 1000;
    limite.tv_nsec += (long)(espera_ms % 1000) * 1000000L;
    if (limite.tv_nsec >= 1000000000L) {
        limite.tv_sec  += 1;
        limite.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&p->mutex);
    uint64_t hechos_antes = p->hechos;

    while (!p->terminado && p->hechos == hechos_antes) {
        if (pthread_cond_timedwait(&p->cambio, &p->mutex, &limite) != 0) {
            break;
        }
    }
    int terminado = p->terminado;
    pthread_mutex_unlock(&p->mutex);
    return terminado;
}
