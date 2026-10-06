/*
 * huffman.h -- interfaz publica del compresor Huffman.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Fase 2: formato por bloques, procesados todavia uno tras otro (sin
 * hilos). En la fase 3 cada bloque lo procesara un hilo distinto.
 *
 * Organizacion de los modulos:
 *   frecuencias.c  -> conteo de cuantas veces aparece cada byte
 *   arbol.c        -> construccion del arbol y tabla de codigos
 *   codificar.c    -> compresion (texto -> bits -> archivo .huff)
 *   decodificar.c  -> descompresion (archivo .huff -> texto)
 *   io.c           -> lectura y escritura de archivos con syscalls
 *   main_huff.c    -> programa de prueba por linea de comandos
 */

#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stdint.h>   /* uint8_t, uint32_t, uint64_t */
#include <stddef.h>   /* size_t                      */
#include <sys/types.h> /* ssize_t                     */

/* Cantidad de simbolos posibles: un byte puede valer de 0 a 255. */
#define HUFF_SIMBOLOS 256

/* ------------------------------------------------------------------ */
/* Formato del archivo .huff (version 2, por bloques)                 */
/* ------------------------------------------------------------------ */
/*
 * El archivo original se divide en bloques de HUFF_TAM_BLOQUE bytes (el
 * ultimo puede ser mas corto). Todos los bloques se codifican con el
 * mismo arbol, construido con las frecuencias de TODO el archivo, pero
 * cada bloque comprimido empieza en un byte nuevo. Asi cada bloque se
 * puede comprimir y descomprimir sin depender de los demas.
 *
 *   Campo                  Tipo                  Bytes
 *   ---------------------  --------------------  ---------------
 *   firma "HUF2"           4 caracteres          4
 *   tamano original        uint64_t              8
 *   tamano de bloque       uint64_t              8
 *   numero de bloques      uint64_t              8
 *   tabla de frecuencias   uint64_t[256]         2048
 *   tabla de tamanos       uint64_t[n_bloques]   8 * n_bloques
 *   bloque 0, bloque 1...  bytes                 variable
 *
 * La tabla de tamanos guarda cuantos bytes comprimidos ocupa cada bloque.
 * Con ella se calcula donde empieza cada bloque sin decodificar los
 * anteriores, que es lo que permite descomprimir en paralelo.
 *
 * La tabla de frecuencias se guarda completa (tamano fijo) para
 * simplificar el codigo; el costo es despreciable en archivos grandes.
 */
#define HUFF_FIRMA        "HUF2"
#define HUFF_FIRMA_LARGO  4
#define HUFF_ENCABEZADO   (HUFF_FIRMA_LARGO + 3 * sizeof(uint64_t) \
                           + HUFF_SIMBOLOS * sizeof(uint64_t))

/* Bytes del archivo original que contiene cada bloque (64 KiB). */
#define HUFF_TAM_BLOQUE   (64 * 1024)

/* Largo maximo de un codigo: es lo que cabe en el campo 'bits' de Codigo. */
#define HUFF_MAX_LARGO    32

/* ------------------------------------------------------------------ */
/* Estructuras de datos                                               */
/* ------------------------------------------------------------------ */

/*
 * Nodo del arbol de Huffman.
 * En las hojas 'simbolo' es el byte (0..255).
 * En los nodos internos 'simbolo' vale -1 y solo importan los hijos.
 */
typedef struct Nodo {
    uint64_t     frecuencia;
    int          simbolo;
    struct Nodo *izq;
    struct Nodo *der;
} Nodo;

/*
 * Codigo de Huffman de un simbolo, guardado como bits en un entero
 * en lugar de un texto de '0' y '1'.
 *
 *   Ejemplo: el codigo "101" se guarda como bits = 0b101, largo = 3.
 *
 * largo = 0 significa que el simbolo no aparece en el archivo.
 */
typedef struct {
    uint32_t bits;    /* TODO: justificar si 32 bits alcanzan */
    int      largo;
} Codigo;

/* ------------------------------------------------------------------ */
/* Frecuencias  (frecuencias.c)                                       */
/* ------------------------------------------------------------------ */

/*
 * Suma a 'freq' las apariciones de cada byte de 'buf'.
 * No pone 'freq' en cero: quien llama debe inicializarlo. Asi la misma
 * funcion sirve para acumular varios bloques (se usara en la fase 3).
 */
void contar_frecuencias(const unsigned char *buf, size_t n,
                        uint64_t freq[HUFF_SIMBOLOS]);

/* ------------------------------------------------------------------ */
/* Arbol y codigos  (arbol.c)                                         */
/* ------------------------------------------------------------------ */

/*
 * Construye el arbol a partir de la tabla de frecuencias.
 * Retorna NULL si todas las frecuencias son cero (archivo vacio).
 * El arbol debe salir identico siempre que las frecuencias sean
 * iguales, porque el descompresor lo reconstruye por su cuenta.
 */
Nodo *construir_arbol(const uint64_t freq[HUFF_SIMBOLOS]);

/* Llena 'tabla' con el codigo de cada simbolo recorriendo el arbol. */
void generar_codigos(const Nodo *raiz, Codigo tabla[HUFF_SIMBOLOS]);

/* Libera todos los nodos del arbol (recorrido en postorden). */
void liberar_arbol(Nodo *raiz);

/* ------------------------------------------------------------------ */
/* Entrada y salida  (io.c)                                           */
/* ------------------------------------------------------------------ */

/*
 * Lee un archivo completo a memoria. En *buf queda un bloque reservado
 * con malloc que quien llama debe liberar con free. Retorna 0 o -1.
 */
int huff_leer_archivo(const char *ruta, unsigned char **buf, size_t *n);

/*
 * Escribe los n bytes de buf en fd, repitiendo write las veces que haga
 * falta. Retorna n si todo se escribio, o -1 si hubo error.
 */
ssize_t huff_escribir_todo(int fd, const void *buf, size_t n);

/* ------------------------------------------------------------------ */
/* Compresion y descompresion  (codificar.c, decodificar.c)           */
/* ------------------------------------------------------------------ */

/* Ambas retornan 0 si todo salio bien y -1 si hubo error. */
int huff_comprimir(const char *ruta_entrada, const char *ruta_salida);
int huff_descomprimir(const char *ruta_entrada, const char *ruta_salida);

#endif /* HUFFMAN_H */