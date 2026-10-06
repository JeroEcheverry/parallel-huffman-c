/*
 * huffman.h -- funciones y estructuras del compresor Huffman.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Fase 1: version secuencial, sin hilos todavia. Sirve de referencia
 * para comparar despues con la version con hilos.
 *
 * Que hace cada archivo:
 *   frecuencias.c  -> cuenta cuantas veces sale cada byte
 *   arbol.c        -> arma el arbol de Huffman y la tabla de codigos
 *   codificar.c    -> comprime (texto -> bits -> archivo .huff)
 *   decodificar.c  -> descomprime (archivo .huff -> texto)
 *   main_huff.c    -> programa de prueba por linea de comandos
 */

#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stdint.h>   /* uint8_t, uint32_t, uint64_t */
#include <stddef.h>   /* size_t                      */

/* Cantidad de simbolos posibles: un byte puede valer de 0 a 255. */
#define HUFF_SIMBOLOS 256

/*
 * Formato del archivo .huff:
 *   [firma "HUF1"] [tamano original: uint64_t] [frecuencias: 256 x uint64_t] [datos]
 * La firma permite reconocer que el archivo si es un .huff valido.
 */
#define HUFF_FIRMA        "HUF1"
#define HUFF_FIRMA_LARGO  4

/* Bytes que ocupa el encabezado completo, antes de los datos. */
#define HUFF_ENCABEZADO   (HUFF_FIRMA_LARGO + sizeof(uint64_t) + \
                           HUFF_SIMBOLOS * sizeof(uint64_t))

/* Largo maximo de un codigo: los bits se guardan en un uint32_t. */
#define HUFF_MAX_LARGO    32

/* ------------------------------------------------------------------ */
/* Estructuras de datos                                               */
/* ------------------------------------------------------------------ */

/*
 * Nodo del arbol de Huffman.
 * Si es una hoja, 'simbolo' guarda el byte (0..255).
 * Si es un nodo interno, 'simbolo' queda en -1 y lo que importa son
 * los hijos.
 */
typedef struct Nodo {
    uint64_t     frecuencia;
    int          simbolo;
    struct Nodo *izq;
    struct Nodo *der;
} Nodo;

/*
 * Codigo de Huffman de un simbolo. Se guarda como bits dentro de un
 * entero, no como texto de '0' y '1'.
 *
 *   Ejemplo: el codigo "101" queda como bits = 0b101, largo = 3.
 *
 * largo = 0 quiere decir que el simbolo no aparece en el archivo.
 * Con 32 bits alcanza de sobra: un codigo de Huffman tan largo
 * necesitaria un archivo gigante con simbolos muy desbalanceados.
 */
typedef struct {
    uint32_t bits;
    int      largo;
} Codigo;

/* ------------------------------------------------------------------ */
/* Frecuencias  (frecuencias.c)                                       */
/* ------------------------------------------------------------------ */

/*
 * Suma a 'freq' las apariciones de cada byte de 'buf'.
 * No pone 'freq' en cero antes: eso lo debe hacer quien llama la
 * funcion. Asi se puede usar varias veces para ir acumulando.
 */
void contar_frecuencias(const unsigned char *buf, size_t n,
                        uint64_t freq[HUFF_SIMBOLOS]);

/* ------------------------------------------------------------------ */
/* Arbol y codigos  (arbol.c)                                         */
/* ------------------------------------------------------------------ */

/*
 * Arma el arbol a partir de la tabla de frecuencias.
 * Devuelve NULL si todas las frecuencias son cero (archivo vacio).
 * Con las mismas frecuencias siempre debe salir el mismo arbol,
 * porque el descompresor lo vuelve a armar por su lado.
 */
Nodo *construir_arbol(const uint64_t freq[HUFF_SIMBOLOS]);

/* Llena 'tabla' con el codigo de cada simbolo recorriendo el arbol. */
void generar_codigos(const Nodo *raiz, Codigo tabla[HUFF_SIMBOLOS]);

/* Libera todos los nodos del arbol (recorrido en postorden). */
void liberar_arbol(Nodo *raiz);

/* ------------------------------------------------------------------ */
/* Compresion y descompresion  (codificar.c, decodificar.c)           */
/* ------------------------------------------------------------------ */

/* Ambas retornan 0 si todo salio bien y -1 si hubo error. */
int huff_comprimir(const char *ruta_entrada, const char *ruta_salida);
int huff_descomprimir(const char *ruta_entrada, const char *ruta_salida);

/* ------------------------------------------------------------------ */
/* Lectura y escritura de archivos  (io.c)                            */
/* ------------------------------------------------------------------ */

/*
 * Lee el archivo completo a memoria. En '*buf' deja un bloque reservado
 * con malloc (quien llama debe liberarlo con free) y en '*n' su tamano.
 * Retorna 0 si todo salio bien y -1 si hubo error.
 */
int huff_leer_archivo(const char *ruta, unsigned char **buf, size_t *n);

/*
 * Escribe los 'n' bytes de 'buf' en 'fd'. write() puede escribir menos
 * de lo pedido, por eso se repite hasta terminar.
 * Retorna 0 si todo salio bien y -1 si hubo error.
 */
int huff_escribir_todo(int fd, const void *buf, size_t n);

#endif /* HUFFMAN_H */