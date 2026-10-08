/* Tipos y funciones que usa el compresor Huffman. */

#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <pthread.h>

/* Valores principales del formato y del compresor. */
#define HUFF_SIMBOLOS 256

/* Datos que guarda el encabezado del archivo comprimido. */
/*
 * Incluye la firma, el tamano original, las frecuencias de los bytes
 * y el tamano de cada bloque comprimido.
 */
#define HUFF_FIRMA        "HUF2"
#define HUFF_FIRMA_LARGO  4
#define HUFF_ENCABEZADO   (HUFF_FIRMA_LARGO + 3 * sizeof(uint64_t) \
                           + HUFF_SIMBOLOS * sizeof(uint64_t))

/* Tamano de cada bloque del archivo original. */
#define HUFF_TAM_BLOQUE   (64 * 1024)

/* Limites para codigos y cantidad de hilos. */
#define HUFF_MAX_LARGO    64
#define HUFF_MAX_HILOS    64

/* Nodo del arbol y codigo asignado a cada byte. */
typedef struct Nodo {
    uint64_t     frecuencia;
    int          simbolo;
    struct Nodo *izq;
    struct Nodo *der;
} Nodo;

typedef struct {
    uint64_t bits;
    int      largo;
} Codigo;

/* Conteo de apariciones de los bytes. */

void contar_frecuencias(const unsigned char *buf, size_t n,
                        uint64_t freq[HUFF_SIMBOLOS]);

/* Construccion del arbol y generacion de codigos. */
Nodo *construir_arbol(const uint64_t freq[HUFF_SIMBOLOS]);

void generar_codigos(const Nodo *raiz, Codigo tabla[HUFF_SIMBOLOS]);
void liberar_arbol(Nodo *raiz);

/* Lectura y escritura de archivos. */
int huff_leer_archivo(const char *ruta, unsigned char **buf, size_t *n);

ssize_t huff_escribir_todo(int fd, const void *buf, size_t n);

/* Estado de avance que comparten los hilos. */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cambio;
    uint64_t        hechos;
    uint64_t        total;
    int             terminado;
    int             cancelar;
} HuffProgreso;

void huff_progreso_iniciar(HuffProgreso *p);
void huff_progreso_destruir(HuffProgreso *p);
void huff_progreso_fijar_total(HuffProgreso *p, uint64_t total);
void huff_progreso_avanzar(HuffProgreso *p, uint64_t n);
void huff_progreso_terminar(HuffProgreso *p);
void huff_progreso_cancelar(HuffProgreso *p);
int  huff_progreso_cancelado(HuffProgreso *p);

void huff_progreso_leer(HuffProgreso *p, uint64_t *hechos, uint64_t *total,
                        int *terminado);

int  huff_progreso_esperar(HuffProgreso *p, int espera_ms);

/* Funcion que procesa una tarea del pool. */
typedef int (*PoolTarea)(void *contexto, uint64_t indice);

/* Datos que necesita el pool para repartir el trabajo. */
typedef struct {
    pthread_t      *hilos;
    int             n_hilos;
    pthread_mutex_t mutex;
    uint64_t        siguiente;
    uint64_t        n_tareas;
    int             error;
    int             demora_ms;
    PoolTarea       tarea;
    void           *contexto;
} Pool;

/* Funciones para iniciar el pool y esperar a que termine. */
int huff_hilos_por_defecto(void);

int pool_iniciar(Pool *pool, int n_hilos, uint64_t n_tareas,
                 PoolTarea tarea, void *contexto);

int pool_esperar(Pool *pool);
int pool_ejecutar(int n_hilos, uint64_t n_tareas, PoolTarea tarea, void *contexto);

/* Funciones principales para comprimir y descomprimir. */
int huff_comprimir(const char *ruta_entrada, const char *ruta_salida,
                   int n_hilos, HuffProgreso *progreso);
int huff_descomprimir(const char *ruta_entrada, const char *ruta_salida,
                      int n_hilos, HuffProgreso *progreso);

#endif
