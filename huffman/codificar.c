/*
 * codificar.c -- compresion de un archivo con Huffman (version 1, secuencial).
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Pasos:
 *   1. Leer el archivo completo a memoria.
 *   2. Contar frecuencias, construir el arbol y generar los codigos.
 *   3. Calcular cuantos bits ocupara la salida y reservar esa memoria.
 *   4. Reemplazar cada byte por su codigo, bit por bit.
 *   5. Escribir el encabezado y los datos en el archivo .huff.
 */
#include "huffman.h"

#include <fcntl.h>     /* open, O_WRONLY, O_CREAT, O_TRUNC */
#include <stdio.h>     /* fprintf, perror                  */
#include <stdlib.h>    /* calloc, free                     */
#include <unistd.h>    /* close                            */

/*
 * Total de bits que ocupara la salida: cada simbolo aparece freq[s] veces
 * y cada aparicion usa tabla[s].largo bits. Conocer este numero antes de
 * codificar permite reservar la memoria exacta de una sola vez.
 */
static uint64_t contar_bits(const uint64_t freq[HUFF_SIMBOLOS],
                            const Codigo tabla[HUFF_SIMBOLOS])
{
    uint64_t total = 0;
    for (int s = 0; s < HUFF_SIMBOLOS; s++) {
        total += freq[s] * (uint64_t)tabla[s].largo;
    }
    return total;
}

/*
 * Escribe en 'salida' el codigo de cada byte de 'entrada'.
 * Los bits se colocan de izquierda a derecha dentro de cada byte
 * (el primer bit va en la posicion 7), igual que el BitStream de la
 * version C++. 'salida' debe venir llena de ceros: solo se encienden
 * los bits que valen 1.
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

/* Escribe firma, tamano original y tabla de frecuencias. Retorna 0 o -1. */
static int escribir_encabezado(int fd, uint64_t tam_original,
                               const uint64_t freq[HUFF_SIMBOLOS])
{
    if (huff_escribir_todo(fd, HUFF_FIRMA, HUFF_FIRMA_LARGO) < 0)                    return -1;
    if (huff_escribir_todo(fd, &tam_original, sizeof(tam_original)) < 0)             return -1;
    if (huff_escribir_todo(fd, freq, HUFF_SIMBOLOS * sizeof(uint64_t)) < 0)          return -1;
    return 0;
}

int huff_comprimir(const char *ruta_entrada, const char *ruta_salida)
{
    /*
     * Todos los recursos se declaran al inicio en un estado "vacio".
     * Ante cualquier error se salta a 'fin', donde se libera solo lo
     * que alcanzo a reservarse. Asi no hay fugas en ningun camino.
     */
    unsigned char *entrada = NULL;
    unsigned char *salida  = NULL;
    Nodo          *raiz    = NULL;
    int            fd      = -1;
    int            resultado = -1;

    size_t   n = 0;
    uint64_t freq[HUFF_SIMBOLOS] = {0};
    Codigo   tabla[HUFF_SIMBOLOS];
    uint64_t total_bits  = 0;
    size_t   total_bytes = 0;

    /* 1. Leer el archivo. */
    if (huff_leer_archivo(ruta_entrada, &entrada, &n) < 0) {
        goto fin;
    }

    /* 2. Frecuencias, arbol y codigos. */
    contar_frecuencias(entrada, n, freq);

    raiz = construir_arbol(freq);
    if (raiz == NULL && n > 0) {
        /* Habia datos pero no se pudo construir el arbol: falta de memoria. */
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

    /* 3. Reservar la salida exacta. calloc la entrega llena de ceros. */
    total_bits  = contar_bits(freq, tabla);
    total_bytes = (size_t)((total_bits + 7) / 8);   /* redondeo hacia arriba */

    salida = calloc(total_bytes > 0 ? total_bytes : 1, 1);
    if (salida == NULL) {
        perror("calloc");
        goto fin;
    }

    /* 4. Codificar. */
    codificar_datos(entrada, n, tabla, salida);

    /* 5. Escribir el archivo .huff. 0644: lectura y escritura para el dueno. */
    fd = open(ruta_salida, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(ruta_salida);
        goto fin;
    }
    if (escribir_encabezado(fd, (uint64_t)n, freq) < 0 ||
        huff_escribir_todo(fd, salida, total_bytes) < 0) {
        perror("write");
        goto fin;
    }

    resultado = 0;

fin:
    if (fd >= 0) close(fd);
    free(salida);          /* free(NULL) no hace nada, es seguro */
    free(entrada);
    liberar_arbol(raiz);
    return resultado;
}