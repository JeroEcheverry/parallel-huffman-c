/* Comprime el archivo por bloques usando varios hilos. */
#include "huffman.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Datos de cada bloque y del trabajo compartido. */
typedef struct {
    const unsigned char *entrada;
    size_t               n_entrada;
    uint64_t             freq[HUFF_SIMBOLOS];
    unsigned char       *salida;
    size_t               n_salida;
    int                  listo;
} Bloque;

typedef struct {
    Bloque         *bloques;
    uint64_t        n_bloques;
    Codigo          tabla[HUFF_SIMBOLOS];
    HuffProgreso   *progreso;

    pthread_mutex_t mutex;
    pthread_cond_t  bloque_listo;
    int             error;
} Compresion;

/* Convierte los bytes originales en sus codigos de Huffman. */
static void codificar_datos(const unsigned char *entrada, size_t n,
                            const Codigo tabla[HUFF_SIMBOLOS],
                            unsigned char *salida)
{
    uint64_t pos = 0;

    for (size_t i = 0; i < n; i++) {
        Codigo c = tabla[entrada[i]];

        for (int b = c.largo - 1; b >= 0; b--) {
            if ((c.bits >> b) & 1) {
                salida[pos / 8] |= (unsigned char)(0x80 >> (pos % 8));
            }
            pos++;
        }
    }
}

/* Tareas que ejecuta el pool para contar y codificar bloques. */

static void avisar_error(Compresion *c)
{
    pthread_mutex_lock(&c->mutex);
    c->error = 1;
    pthread_cond_broadcast(&c->bloque_listo);
    pthread_mutex_unlock(&c->mutex);
}

static int debe_parar(Compresion *c)
{
    pthread_mutex_lock(&c->mutex);
    int error = c->error;
    pthread_mutex_unlock(&c->mutex);
    return error || huff_progreso_cancelado(c->progreso);
}

static int tarea_contar(void *contexto, uint64_t i)
{
    Compresion *c = contexto;
    if (debe_parar(c)) return -1;

    Bloque *b = &c->bloques[i];
    contar_frecuencias(b->entrada, b->n_entrada, b->freq);

    huff_progreso_avanzar(c->progreso, 1);
    return 0;
}

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

/* Escritura del encabezado y de los bloques comprimidos. */

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

/* Espera a que cada bloque este listo y lo escribe en el orden original. */
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

        if (huff_escribir_todo(fd, b->salida, b->n_salida) < 0) {
            perror("write");
            avisar_error(c);
            return -1;
        }
        free(b->salida);
        b->salida = NULL;

        huff_progreso_avanzar(c->progreso, 1);

        if (huff_progreso_cancelado(c->progreso)) {
            avisar_error(c);
            return -1;
        }
    }
    return 0;
}

/* Proceso completo de compresion. */

int huff_comprimir(const char *ruta_entrada, const char *ruta_salida,
                   int n_hilos, HuffProgreso *progreso)
{
    unsigned char *entrada   = NULL;
    Nodo          *raiz      = NULL;
    int            fd        = -1;
    int            resultado = -1;
    int            sync_ok   = 0;

    Compresion c = { 0 };
    c.progreso = progreso;
    Pool pool;
    int  r_escritor, r_pool;

    size_t   n = 0;
    uint64_t freq[HUFF_SIMBOLOS] = {0};

    /* Leer el archivo y preparar sus bloques. */
    if (huff_leer_archivo(ruta_entrada, &entrada, &n) < 0) {
        goto fin;
    }
    c.n_bloques = (n + HUFF_TAM_BLOQUE - 1) / HUFF_TAM_BLOQUE;

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

    huff_progreso_fijar_total(progreso, 2 * c.n_bloques);

    /* Contar frecuencias por bloque y sumarlas. */
    if (pool_ejecutar(n_hilos, c.n_bloques, tarea_contar, &c) < 0) {
        goto fin;
    }

    for (uint64_t i = 0; i < c.n_bloques; i++) {
        for (int s = 0; s < HUFF_SIMBOLOS; s++) {
            freq[s] += c.bloques[i].freq[s];
        }
    }

    /* Crear el arbol y calcular los codigos que se usaran. */
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

    /* Calcular el espacio necesario para cada bloque comprimido. */
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

    /* Codificar en paralelo y guardar los bloques en orden. */
    if (pool_iniciar(&pool, n_hilos, c.n_bloques, tarea_codificar, &c) < 0) {
        goto fin;
    }
    r_escritor = escribir_en_orden(&c, fd);
    r_pool     = pool_esperar(&pool);
    if (r_escritor < 0 || r_pool < 0) {
        goto fin;
    }

    resultado = 0;

fin:
    if (fd >= 0) {
        close(fd);
        if (resultado < 0) {
            unlink(ruta_salida);
        }
    }
    if (c.bloques != NULL) {
        for (uint64_t i = 0; i < c.n_bloques; i++) {
            free(c.bloques[i].salida);
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
