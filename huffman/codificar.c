/*
 * codificar.c -- compresion concurrente por bloques con Huffman.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Etapas:
 *   1. Leer el archivo completo y dividirlo en bloques.
 *   2. [PARALELO] Cada hilo cuenta las frecuencias de los bloques que
 *      toma, cada uno en la tabla propia del bloque (reduccion local).
 *   3. Sumar las tablas locales en la tabla global (hilo principal).
 *   4. Construir un solo arbol y generar los codigos.
 *   5. Calcular cuantos bytes ocupara cada bloque comprimido y escribir
 *      el encabezado con esa tabla de tamanos.
 *   6. [PARALELO] Los hilos codifican bloques en cualquier orden mientras
 *      el hilo principal, como coordinador, escribe en el archivo cada
 *      bloque en orden (0, 1, 2...) apenas esta listo.
 *
 * Sincronizacion:
 *   - Etapa 2: cada bloque tiene su propia tabla de frecuencias, asi que
 *     ningun par de hilos escribe en la misma memoria y no se necesita
 *     mutex para contar. El pool_esperar (join) entre la etapa 2 y la 3
 *     garantiza que todas las tablas esten completas antes de sumarlas.
 *   - Etapa 6: el mutex 'mutex' protege los campos 'listo' de los bloques
 *     y 'error'. El escritor duerme en la variable de condicion
 *     'bloque_listo' hasta que el bloque que le toca este terminado; cada
 *     trabajador la senala al terminar un bloque. No hay espera activa.
 */
#include "huffman.h"

#include <fcntl.h>     /* open, O_WRONLY, O_CREAT, O_TRUNC */
#include <stdio.h>     /* fprintf, perror                  */
#include <stdlib.h>    /* calloc, free                     */
#include <unistd.h>    /* close, unlink                    */

/* Estado de un bloque durante la compresion. */
typedef struct {
    const unsigned char *entrada;     /* inicio del bloque dentro del archivo original */
    size_t               n_entrada;   /* bytes originales del bloque                   */
    uint64_t             freq[HUFF_SIMBOLOS];   /* frecuencias de este bloque solamente */
    unsigned char       *salida;      /* bytes comprimidos (calloc), NULL si no hay    */
    size_t               n_salida;    /* bytes comprimidos del bloque                  */
    int                  listo;       /* 1 cuando 'salida' ya esta completa            */
} Bloque;

/* Datos que comparten el hilo coordinador y los hilos trabajadores. */
typedef struct {
    Bloque         *bloques;
    uint64_t        n_bloques;
    Codigo          tabla[HUFF_SIMBOLOS];
    HuffProgreso   *progreso;

    pthread_mutex_t mutex;          /* protege bloques[i].listo y 'error' */
    pthread_cond_t  bloque_listo;   /* se senala al terminar cada bloque  */
    int             error;          /* 1 si alguien fallo o se cancelo    */
} Compresion;

/* ------------------------------------------------------------------ */
/* Codificacion de bits                                               */
/* ------------------------------------------------------------------ */

/*
 * Escribe en 'salida' el codigo de cada byte de 'entrada'.
 * Los bits se colocan de izquierda a derecha dentro de cada byte
 * (el primer bit va en la posicion 7). 'salida' debe venir llena de
 * ceros: solo se encienden los bits que valen 1.
 */
static void codificar_datos(const unsigned char *entrada, size_t n,
                            const Codigo tabla[HUFF_SIMBOLOS],
                            unsigned char *salida)
{
    uint64_t pos = 0;   /* numero del siguiente bit a escribir en 'salida' */

    for (size_t i = 0; i < n; i++) {
        Codigo c = tabla[entrada[i]];

        /* Se recorren los bits del codigo del mas significativo al menos. */
        for (int b = c.largo - 1; b >= 0; b--) {
            if ((c.bits >> b) & 1) {
                salida[pos / 8] |= (unsigned char)(0x80 >> (pos % 8));
            }
            pos++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Tareas que ejecutan los hilos del pool                             */
/* ------------------------------------------------------------------ */

/* Marca el error y despierta al escritor para que no espere para siempre. */
static void avisar_error(Compresion *c)
{
    pthread_mutex_lock(&c->mutex);
    c->error = 1;
    pthread_cond_broadcast(&c->bloque_listo);
    pthread_mutex_unlock(&c->mutex);
}

/* Retorna 1 si hay que dejar de trabajar (error o cancelacion). */
static int debe_parar(Compresion *c)
{
    pthread_mutex_lock(&c->mutex);
    int error = c->error;
    pthread_mutex_unlock(&c->mutex);
    return error || huff_progreso_cancelado(c->progreso);
}

/* Etapa 2: frecuencias de un bloque, en la tabla propia del bloque. */
static int tarea_contar(void *contexto, uint64_t i)
{
    Compresion *c = contexto;
    if (debe_parar(c)) return -1;

    Bloque *b = &c->bloques[i];
    contar_frecuencias(b->entrada, b->n_entrada, b->freq);

    huff_progreso_avanzar(c->progreso, 1);
    return 0;
}

/* Etapa 6: codificar un bloque y avisar al escritor que esta listo. */
static int tarea_codificar(void *contexto, uint64_t i)
{
    Compresion *c = contexto;
    if (debe_parar(c)) {
        avisar_error(c);
        return -1;
    }

    Bloque *b = &c->bloques[i];
    b->salida = calloc(b->n_salida > 0 ? b->n_salida : 1, 1);
    if (b->salida == NULL) {
        avisar_error(c);
        return -1;
    }
    codificar_datos(b->entrada, b->n_entrada, c->tabla, b->salida);

    pthread_mutex_lock(&c->mutex);
    b->listo = 1;
    pthread_cond_broadcast(&c->bloque_listo);
    pthread_mutex_unlock(&c->mutex);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Escritura                                                          */
/* ------------------------------------------------------------------ */

/* Escribe el encabezado completo, incluida la tabla de tamanos. Retorna 0 o -1. */
static int escribir_encabezado(int fd, uint64_t tam_original,
                               const uint64_t freq[HUFF_SIMBOLOS],
                               const Compresion *c)
{
    uint64_t tam_bloque = HUFF_TAM_BLOQUE;
    uint64_t n_bloques  = c->n_bloques;

    if (huff_escribir_todo(fd, HUFF_FIRMA, HUFF_FIRMA_LARGO) < 0)            return -1;
    if (huff_escribir_todo(fd, &tam_original, sizeof(uint64_t)) < 0)         return -1;
    if (huff_escribir_todo(fd, &tam_bloque,   sizeof(uint64_t)) < 0)         return -1;
    if (huff_escribir_todo(fd, &n_bloques,    sizeof(uint64_t)) < 0)         return -1;
    if (huff_escribir_todo(fd, freq, HUFF_SIMBOLOS * sizeof(uint64_t)) < 0)  return -1;

    for (uint64_t i = 0; i < n_bloques; i++) {
        uint64_t tam = c->bloques[i].n_salida;
        if (huff_escribir_todo(fd, &tam, sizeof(uint64_t)) < 0)              return -1;
    }
    return 0;
}

/*
 * Hilo coordinador de la etapa 6: escribe los bloques en orden.
 * Si el bloque i todavia no esta listo, el hilo duerme en la variable de
 * condicion hasta que un trabajador lo termine. Los bloques que terminan
 * antes de su turno simplemente esperan en memoria.
 */
static int escribir_en_orden(Compresion *c, int fd)
{
    for (uint64_t i = 0; i < c->n_bloques; i++) {
        Bloque *b = &c->bloques[i];

        pthread_mutex_lock(&c->mutex);
        while (!b->listo && !c->error) {
            pthread_cond_wait(&c->bloque_listo, &c->mutex);
        }
        int error = c->error;
        pthread_mutex_unlock(&c->mutex);

        if (error) return -1;

        /* La escritura se hace fuera del mutex: el disco es lento y los
           trabajadores no deben esperar por el. Ningun otro hilo toca
           este bloque despues de marcarlo listo. */
        if (huff_escribir_todo(fd, b->salida, b->n_salida) < 0) {
            perror("write");
            avisar_error(c);
            return -1;
        }
        free(b->salida);          /* ya esta en disco: se libera de una vez */
        b->salida = NULL;

        huff_progreso_avanzar(c->progreso, 1);

        if (huff_progreso_cancelado(c->progreso)) {
            avisar_error(c);
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Funcion publica                                                    */
/* ------------------------------------------------------------------ */

int huff_comprimir(const char *ruta_entrada, const char *ruta_salida,
                   int n_hilos, HuffProgreso *progreso)
{
    /*
     * Todos los recursos se declaran al inicio en un estado "vacio".
     * Ante cualquier error se salta a 'fin', donde se libera solo lo
     * que alcanzo a reservarse. Asi no hay fugas en ningun camino.
     */
    unsigned char *entrada   = NULL;
    Nodo          *raiz      = NULL;
    int            fd        = -1;
    int            resultado = -1;
    int            sync_ok   = 0;    /* 1 si se inicializaron mutex y cond */

    Compresion c = { 0 };
    c.progreso = progreso;
    Pool pool;
    int  r_escritor, r_pool;

    size_t   n = 0;
    uint64_t freq[HUFF_SIMBOLOS] = {0};

    /* 1. Leer el archivo y dividirlo en bloques. */
    if (huff_leer_archivo(ruta_entrada, &entrada, &n) < 0) {
        goto fin;
    }
    c.n_bloques = (n + HUFF_TAM_BLOQUE - 1) / HUFF_TAM_BLOQUE;   /* redondeo hacia arriba */

    c.bloques = calloc(c.n_bloques > 0 ? c.n_bloques : 1, sizeof(Bloque));
    if (c.bloques == NULL) {
        perror("calloc");
        goto fin;
    }
    for (uint64_t i = 0; i < c.n_bloques; i++) {
        size_t inicio = (size_t)(i * HUFF_TAM_BLOQUE);
        c.bloques[i].entrada   = entrada + inicio;
        c.bloques[i].n_entrada = (n - inicio < HUFF_TAM_BLOQUE) ? n - inicio : HUFF_TAM_BLOQUE;
    }

    pthread_mutex_init(&c.mutex, NULL);
    pthread_cond_init(&c.bloque_listo, NULL);
    sync_ok = 1;

    /* Avance total: cada bloque se cuenta una vez y se escribe una vez. */
    huff_progreso_fijar_total(progreso, 2 * c.n_bloques);

    /* 2. Frecuencias locales en paralelo. El join es la barrera. */
    if (pool_ejecutar(n_hilos, c.n_bloques, tarea_contar, &c) < 0) {
        goto fin;
    }

    /* 3. Reduccion: el total es la suma de las tablas locales. */
    for (uint64_t i = 0; i < c.n_bloques; i++) {
        for (int s = 0; s < HUFF_SIMBOLOS; s++) {
            freq[s] += c.bloques[i].freq[s];
        }
    }

    /* 4. Un solo arbol para todo el archivo. */
    raiz = construir_arbol(freq);
    if (raiz == NULL && n > 0) {
        fprintf(stderr, "Error: no hay memoria para el arbol\n");
        goto fin;
    }
    generar_codigos(raiz, c.tabla);

    for (int s = 0; s < HUFF_SIMBOLOS; s++) {
        if (c.tabla[s].largo > HUFF_MAX_LARGO) {
            fprintf(stderr, "Error: codigo de %d bits, el maximo es %d\n",
                    c.tabla[s].largo, HUFF_MAX_LARGO);
            goto fin;
        }
    }

    /* 5. Tamano de cada bloque comprimido: freq[s] apariciones de
          tabla[s].largo bits cada una, redondeado a bytes completos. */
    for (uint64_t i = 0; i < c.n_bloques; i++) {
        uint64_t bits = 0;
        for (int s = 0; s < HUFF_SIMBOLOS; s++) {
            bits += c.bloques[i].freq[s] * (uint64_t)c.tabla[s].largo;
        }
        c.bloques[i].n_salida = (size_t)((bits + 7) / 8);
    }

    fd = open(ruta_salida, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(ruta_salida);
        goto fin;
    }
    if (escribir_encabezado(fd, (uint64_t)n, freq, &c) < 0) {
        perror("write");
        goto fin;
    }

    /* 6. Los trabajadores codifican mientras este hilo escribe en orden. */
    if (pool_iniciar(&pool, n_hilos, c.n_bloques, tarea_codificar, &c) < 0) {
        goto fin;
    }
    r_escritor = escribir_en_orden(&c, fd);
    r_pool     = pool_esperar(&pool);   /* join de todos los trabajadores */
    if (r_escritor < 0 || r_pool < 0) {
        goto fin;
    }

    resultado = 0;

fin:
    if (fd >= 0) {
        close(fd);
        if (resultado < 0) {
            unlink(ruta_salida);   /* no se deja un .huff a medias */
        }
    }
    if (c.bloques != NULL) {
        for (uint64_t i = 0; i < c.n_bloques; i++) {
            free(c.bloques[i].salida);   /* free(NULL) no hace nada, es seguro */
        }
        free(c.bloques);
    }
    if (sync_ok) {
        pthread_cond_destroy(&c.bloque_listo);
        pthread_mutex_destroy(&c.mutex);
    }
    free(entrada);
    liberar_arbol(raiz);
    huff_progreso_terminar(progreso);
    return resultado;
}
