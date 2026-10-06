/*
 * decodificar.c -- descompresion de un archivo .huff (version 1, secuencial).
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Pasos:
 *   1. Leer el archivo .huff completo a memoria.
 *   2. Validar la firma y extraer tamano original y frecuencias.
 *   3. Reconstruir el mismo arbol que uso el compresor.
 *   4. Recorrer el arbol bit a bit hasta recuperar todos los bytes.
 *   5. Escribir el resultado en el archivo de salida.
 */
#include "huffman.h"

#include <fcntl.h>     /* open, O_WRONLY, O_CREAT, O_TRUNC */
#include <stdio.h>     /* fprintf, perror                  */
#include <stdlib.h>    /* malloc, free                     */
#include <string.h>    /* memcmp, memcpy                   */
#include <unistd.h>    /* close                            */

/*
 * Recorre el arbol siguiendo los bits de 'datos' y escribe en 'salida'
 * los 'tam_original' bytes recuperados. Misma idea que Decoder::decode
 * de la version C++: bit 0 -> izquierda, bit 1 -> derecha, al llegar a
 * una hoja se emite su simbolo y se vuelve a la raiz.
 * Retorna 0, o -1 si los datos se acaban antes de tiempo (archivo danado).
 */
static int decodificar_datos(const Nodo *raiz,
                             const unsigned char *datos, size_t n_datos,
                             unsigned char *salida, uint64_t tam_original)
{
    uint64_t total_bits = (uint64_t)n_datos * 8;
    uint64_t pos = 0;   /* numero del siguiente bit a leer de 'datos' */

    /* Caso especial: un solo simbolo. El arbol es una hoja y cada bit "0"
       del archivo representa una aparicion de ese simbolo. */
    if (raiz->izq == NULL && raiz->der == NULL) {
        if (total_bits < tam_original) return -1;
        memset(salida, raiz->simbolo, (size_t)tam_original);
        return 0;
    }

    for (uint64_t i = 0; i < tam_original; i++) {
        const Nodo *nodo = raiz;

        while (nodo->izq != NULL || nodo->der != NULL) {
            if (pos >= total_bits) return -1;   /* se acabaron los datos */

            /* Mismo orden que el compresor: bit mas significativo primero. */
            int bit = (datos[pos / 8] >> (7 - (pos % 8))) & 1;
            pos++;

            nodo = bit ? nodo->der : nodo->izq;
        }
        salida[i] = nodo->simbolo;
    }
    return 0;
}

int huff_descomprimir(const char *ruta_entrada, const char *ruta_salida)
{
    unsigned char *archivo = NULL;
    unsigned char *salida  = NULL;
    Nodo          *raiz    = NULL;
    int            fd      = -1;
    int            resultado = -1;

    size_t   n = 0;
    uint64_t tam_original = 0;
    uint64_t freq[HUFF_SIMBOLOS];
    const unsigned char *datos = NULL;
    size_t               n_datos = 0;

    /* 1. Leer el .huff completo. */
    if (huff_leer_archivo(ruta_entrada, &archivo, &n) < 0) {
        goto fin;
    }

    /* 2. Validar y leer el encabezado. */
    if (n < HUFF_ENCABEZADO || memcmp(archivo, HUFF_FIRMA, HUFF_FIRMA_LARGO) != 0) {
        fprintf(stderr, "Error: '%s' no es un archivo .huff valido\n", ruta_entrada);
        goto fin;
    }
    /* memcpy en vez de un cast de punteros: evita accesos desalineados. */
    memcpy(&tam_original, archivo + HUFF_FIRMA_LARGO, sizeof(tam_original));
    memcpy(freq, archivo + HUFF_FIRMA_LARGO + sizeof(tam_original),
           HUFF_SIMBOLOS * sizeof(uint64_t));

    datos   = archivo + HUFF_ENCABEZADO;
    n_datos = n - HUFF_ENCABEZADO;

    /* 3. Reconstruir el arbol con las mismas frecuencias. */
    raiz = construir_arbol(freq);
    if (raiz == NULL && tam_original > 0) {
        fprintf(stderr, "Error: tabla de frecuencias invalida o sin memoria\n");
        goto fin;
    }

    /* 4. Decodificar. */
    salida = malloc(tam_original > 0 ? (size_t)tam_original : 1);
    if (salida == NULL) {
        perror("malloc");
        goto fin;
    }
    if (tam_original > 0 &&
        decodificar_datos(raiz, datos, n_datos, salida, tam_original) < 0) {
        fprintf(stderr, "Error: datos comprimidos incompletos\n");
        goto fin;
    }

    /* 5. Escribir el resultado. */
    fd = open(ruta_salida, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(ruta_salida);
        goto fin;
    }
    if (huff_escribir_todo(fd, salida, (size_t)tam_original) < 0) {
        perror("write");
        goto fin;
    }

    resultado = 0;

fin:
    if (fd >= 0) close(fd);
    free(salida);
    free(archivo);
    liberar_arbol(raiz);
    return resultado;
}