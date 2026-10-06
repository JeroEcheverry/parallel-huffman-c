/*
 * codificar.c -- compresion por bloques con Huffman (version 2, secuencial).
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Pasos:
 *   1. Leer el archivo completo y dividirlo en bloques.
 *   2. Contar las frecuencias de cada bloque por separado.
 *   3. Sumar las frecuencias de todos los bloques (reduccion).
 *   4. Construir un solo arbol con el total y generar los codigos.
 *   5. Codificar cada bloque en su propio buffer.
 *   6. Escribir encabezado, tabla de tamanos y bloques en orden.
 *
 * Los pasos 2 y 5 trabajan cada bloque sin tocar los demas: en la fase 3
 * se reparten entre varios hilos sin cambiar la logica.
 */
#include "huffman.h"

#include <fcntl.h>     /* open, O_WRONLY, O_CREAT, O_TRUNC */
#include <stdio.h>     /* fprintf, perror                  */
#include <stdlib.h>    /* calloc, free                     */
#include <unistd.h>    /* close                            */

/* Estado de un bloque durante la compresion. */
typedef struct {
    const unsigned char *entrada;     /* inicio del bloque dentro del archivo original */
    size_t               n_entrada;   /* bytes originales del bloque                   */
    uint64_t             freq[HUFF_SIMBOLOS];   /* frecuencias de este bloque solamente */
    unsigned char       *salida;      /* bytes comprimidos (calloc), NULL si no hay    */
    size_t               n_salida;    /* bytes comprimidos del bloque                  */
} Bloque;

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

/*
 * Codifica un bloque. El tamano exacto de la salida se conoce antes de
 * empezar: cada simbolo aparece freq[s] veces en el bloque y cada
 * aparicion ocupa tabla[s].largo bits. El ultimo byte se completa con
 * ceros, de modo que el siguiente bloque empieza en un byte nuevo.
 * Retorna 0, o -1 si no hay memoria.
 */
static int codificar_bloque(Bloque *b, const Codigo tabla[HUFF_SIMBOLOS])
{
    uint64_t bits = 0;
    for (int s = 0; s < HUFF_SIMBOLOS; s++) {
        bits += b->freq[s] * (uint64_t)tabla[s].largo;
    }
    b->n_salida = (size_t)((bits + 7) / 8);   /* redondeo hacia arriba */

    b->salida = calloc(b->n_salida > 0 ? b->n_salida : 1, 1);
    if (b->salida == NULL) {
        return -1;
    }
    codificar_datos(b->entrada, b->n_entrada, tabla, b->salida);
    return 0;
}

/* Escribe el encabezado completo, incluida la tabla de tamanos. Retorna 0 o -1. */
static int escribir_encabezado(int fd, uint64_t tam_original, uint64_t n_bloques,
                               const uint64_t freq[HUFF_SIMBOLOS],
                               const Bloque *bloques)
{
    uint64_t tam_bloque = HUFF_TAM_BLOQUE;

    if (huff_escribir_todo(fd, HUFF_FIRMA, HUFF_FIRMA_LARGO) < 0)            return -1;
    if (huff_escribir_todo(fd, &tam_original, sizeof(uint64_t)) < 0)         return -1;
    if (huff_escribir_todo(fd, &tam_bloque,   sizeof(uint64_t)) < 0)         return -1;
    if (huff_escribir_todo(fd, &n_bloques,    sizeof(uint64_t)) < 0)         return -1;
    if (huff_escribir_todo(fd, freq, HUFF_SIMBOLOS * sizeof(uint64_t)) < 0)  return -1;

    for (uint64_t i = 0; i < n_bloques; i++) {
        uint64_t tam = bloques[i].n_salida;
        if (huff_escribir_todo(fd, &tam, sizeof(uint64_t)) < 0)              return -1;
    }
    return 0;
}

int huff_comprimir(const char *ruta_entrada, const char *ruta_salida)
{
    /*
     * Todos los recursos se declaran al inicio en un estado "vacio".
     * Ante cualquier error se salta a 'fin', donde se libera solo lo
     * que alcanzo a reservarse. Asi no hay fugas en ningun camino.
     */
    unsigned char *entrada   = NULL;
    Bloque        *bloques   = NULL;
    Nodo          *raiz      = NULL;
    int            fd        = -1;
    int            resultado = -1;

    size_t   n = 0;
    uint64_t n_bloques = 0;
    uint64_t freq[HUFF_SIMBOLOS] = {0};
    Codigo   tabla[HUFF_SIMBOLOS];

    /* 1. Leer el archivo y dividirlo en bloques. */
    if (huff_leer_archivo(ruta_entrada, &entrada, &n) < 0) {
        goto fin;
    }
    n_bloques = (n + HUFF_TAM_BLOQUE - 1) / HUFF_TAM_BLOQUE;   /* redondeo hacia arriba */

    /* calloc deja en cero las frecuencias y los punteros de cada bloque. */
    bloques = calloc(n_bloques > 0 ? n_bloques : 1, sizeof(Bloque));
    if (bloques == NULL) {
        perror("calloc");
        goto fin;
    }
    for (uint64_t i = 0; i < n_bloques; i++) {
        size_t inicio = (size_t)(i * HUFF_TAM_BLOQUE);
        bloques[i].entrada   = entrada + inicio;
        bloques[i].n_entrada = (n - inicio < HUFF_TAM_BLOQUE) ? n - inicio : HUFF_TAM_BLOQUE;
    }

    /* 2. Frecuencias locales de cada bloque (en la fase 3: un hilo por bloque). */
    for (uint64_t i = 0; i < n_bloques; i++) {
        contar_frecuencias(bloques[i].entrada, bloques[i].n_entrada, bloques[i].freq);
    }

    /* 3. Reduccion: el total es la suma de las tablas locales. */
    for (uint64_t i = 0; i < n_bloques; i++) {
        for (int s = 0; s < HUFF_SIMBOLOS; s++) {
            freq[s] += bloques[i].freq[s];
        }
    }

    /* 4. Un solo arbol para todo el archivo. */
    raiz = construir_arbol(freq);
    if (raiz == NULL && n > 0) {
        fprintf(stderr, "Error: no hay memoria para el arbol\n");
        goto fin;
    }
    generar_codigos(raiz, tabla);

    for (int s = 0; s < HUFF_SIMBOLOS; s++) {
        if (tabla[s].largo > HUFF_MAX_LARGO) {
            fprintf(stderr, "Error: codigo de %d bits, el maximo es %d\n",
                    tabla[s].largo, HUFF_MAX_LARGO);
            goto fin;
        }
    }

    /* 5. Codificar cada bloque (en la fase 3: un hilo por bloque). */
    for (uint64_t i = 0; i < n_bloques; i++) {
        if (codificar_bloque(&bloques[i], tabla) < 0) {
            perror("calloc");
            goto fin;
        }
    }

    /* 6. Escribir el archivo: encabezado y luego los bloques en orden. */
    fd = open(ruta_salida, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(ruta_salida);
        goto fin;
    }
    if (escribir_encabezado(fd, (uint64_t)n, n_bloques, freq, bloques) < 0) {
        perror("write");
        goto fin;
    }
    for (uint64_t i = 0; i < n_bloques; i++) {
        if (huff_escribir_todo(fd, bloques[i].salida, bloques[i].n_salida) < 0) {
            perror("write");
            goto fin;
        }
    }

    resultado = 0;

fin:
    if (fd >= 0) close(fd);
    if (bloques != NULL) {
        for (uint64_t i = 0; i < n_bloques; i++) {
            free(bloques[i].salida);   /* free(NULL) no hace nada, es seguro */
        }
        free(bloques);
    }
    free(entrada);
    liberar_arbol(raiz);
    return resultado;
}