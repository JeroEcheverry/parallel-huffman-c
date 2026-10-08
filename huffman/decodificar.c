/*
 * decodificar.c -- descompresion concurrente de un archivo .huff por bloques.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Etapas:
 *   1. Leer el archivo .huff completo a memoria.
 *   2. Validar el encabezado y leer la tabla de tamanos.
 *   3. Calcular donde empieza cada bloque comprimido.
 *   4. Reconstruir el mismo arbol que uso el compresor.
 *   5. [PARALELO] Cada hilo decodifica bloques y escribe el resultado en
 *      su posicion de la salida (bloque i -> bytes i*tam_bloque en adelante).
 *   6. Escribir el resultado en el archivo.
 *
 * Sincronizacion: en la etapa 5 cada bloque se escribe en una zona de
 * 'salida' que no se cruza con la de ningun otro bloque, y el arbol solo
 * se lee. Por eso los hilos no necesitan mutex para decodificar; solo el
 * reparto de tareas (pool.c) y el progreso (progreso.c) usan mutex. El
 * join del pool garantiza que la salida este completa antes de escribirla.
 */
#include "huffman.h"

#include <fcntl.h>     /* open, O_WRONLY, O_CREAT, O_TRUNC */
#include <stdio.h>     /* fprintf, perror                  */
#include <stdlib.h>    /* malloc, free                     */
#include <string.h>    /* memcmp, memcpy, memset           */
#include <unistd.h>    /* close, unlink                    */

/* Datos que comparten los hilos durante la etapa 5 (todos de solo lectura,
   salvo la zona de 'salida' que le corresponde a cada bloque). */
typedef struct {
    const Nodo          *raiz;
    const unsigned char *datos;        /* primer byte del bloque 0      */
    const uint64_t      *tamanos;      /* bytes comprimidos por bloque  */
    const uint64_t      *inicio;       /* donde empieza cada bloque     */
    unsigned char       *salida;       /* archivo original reconstruido */
    uint64_t             tam_original;
    uint64_t             tam_bloque;
    HuffProgreso        *progreso;
} Descompresion;

/*
 * Recorre el arbol siguiendo los bits de 'datos' y escribe en 'salida'
 * los 'tam_original' bytes recuperados: bit 0 -> izquierda, bit 1 ->
 * derecha, al llegar a una hoja se emite su simbolo y se vuelve a la raiz.
 * Se usa una vez por bloque.
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
        salida[i] = (unsigned char)nodo->simbolo;
    }
    return 0;
}

/*
 * Llena inicio[i] con la posicion donde empieza el bloque i, contada
 * desde el primer byte de datos (justo despues del encabezado).
 *
 *   Ejemplo: tamanos = {140, 90, 160, 120}  ->  inicio = {0, 140, 230, 390}
 */
static void calcular_inicios(const uint64_t *tamanos, uint64_t n_bloques,
                             uint64_t *inicio)
{
    uint64_t acumulado = 0;   /* bytes ocupados por los bloques ya recorridos */

    for (uint64_t i = 0; i < n_bloques; i++) {
        inicio[i]  = acumulado;      /* el bloque i empieza donde van los anteriores */
        acumulado += tamanos[i];     /* y ahora sumamos lo que ocupa el bloque i     */
    }
}

/* Etapa 5: tarea que ejecuta un hilo del pool para el bloque i. */
static int tarea_decodificar(void *contexto, uint64_t i)
{
    Descompresion *d = contexto;
    if (huff_progreso_cancelado(d->progreso)) return -1;

    uint64_t destino = i * d->tam_bloque;
    uint64_t largo   = d->tam_original - destino < d->tam_bloque
                     ? d->tam_original - destino : d->tam_bloque;

    if (decodificar_datos(d->raiz, d->datos + d->inicio[i], (size_t)d->tamanos[i],
                          d->salida + destino, largo) < 0) {
        fprintf(stderr, "Error: el bloque %llu esta incompleto\n",
                (unsigned long long)i);
        return -1;
    }

    huff_progreso_avanzar(d->progreso, 1);
    return 0;
}

int huff_descomprimir(const char *ruta_entrada, const char *ruta_salida,
                      int n_hilos, HuffProgreso *progreso)
{
    unsigned char *archivo   = NULL;
    unsigned char *salida    = NULL;
    uint64_t      *tamanos   = NULL;
    uint64_t      *inicio    = NULL;
    Nodo          *raiz      = NULL;
    int            fd        = -1;
    int            resultado = -1;

    size_t   n = 0;
    uint64_t tam_original = 0;
    uint64_t tam_bloque   = 0;
    uint64_t n_bloques    = 0;
    uint64_t freq[HUFF_SIMBOLOS];
    const unsigned char *p     = NULL;   /* cursor para leer el encabezado */
    const unsigned char *datos = NULL;
    size_t               n_datos = 0;
    Descompresion        d;

    /* 1. Leer el .huff completo. */
    if (huff_leer_archivo(ruta_entrada, &archivo, &n) < 0) {
        goto fin;
    }

    /* 2. Validar y leer el encabezado. memcpy evita accesos desalineados. */
    if (n < HUFF_ENCABEZADO || memcmp(archivo, HUFF_FIRMA, HUFF_FIRMA_LARGO) != 0) {
        fprintf(stderr, "Error: '%s' no es un archivo .huff valido\n", ruta_entrada);
        goto fin;
    }
    p = archivo + HUFF_FIRMA_LARGO;
    memcpy(&tam_original, p, sizeof(uint64_t));  p += sizeof(uint64_t);
    memcpy(&tam_bloque,   p, sizeof(uint64_t));  p += sizeof(uint64_t);
    memcpy(&n_bloques,    p, sizeof(uint64_t));  p += sizeof(uint64_t);
    memcpy(freq, p, HUFF_SIMBOLOS * sizeof(uint64_t));

    /*
     * Un archivo manipulado podria traer valores absurdos. Antes de usarlos
     * se comprueba que sean coherentes entre si y con el tamano del archivo.
     */
    if (tam_bloque == 0 ||
        n_bloques != (tam_original + tam_bloque - 1) / tam_bloque ||
        n_bloques > (n - HUFF_ENCABEZADO) / sizeof(uint64_t)) {
        fprintf(stderr, "Error: encabezado inconsistente en '%s'\n", ruta_entrada);
        goto fin;
    }

    tamanos = malloc((n_bloques > 0 ? n_bloques : 1) * sizeof(uint64_t));
    inicio  = malloc((n_bloques > 0 ? n_bloques : 1) * sizeof(uint64_t));
    if (tamanos == NULL || inicio == NULL) {
        perror("malloc");
        goto fin;
    }
    memcpy(tamanos, archivo + HUFF_ENCABEZADO, n_bloques * sizeof(uint64_t));

    datos   = archivo + HUFF_ENCABEZADO + n_bloques * sizeof(uint64_t);
    n_datos = n - HUFF_ENCABEZADO - n_bloques * sizeof(uint64_t);

    /* 3. Donde empieza cada bloque, y que ninguno se salga del archivo. */
    calcular_inicios(tamanos, n_bloques, inicio);
    for (uint64_t i = 0; i < n_bloques; i++) {
        if (tamanos[i] > n_datos || inicio[i] > n_datos - tamanos[i]) {
            fprintf(stderr, "Error: el bloque %llu se sale del archivo\n",
                    (unsigned long long)i);
            goto fin;
        }
    }

    /* 4. Reconstruir el arbol con las mismas frecuencias. */
    raiz = construir_arbol(freq);
    if (raiz == NULL && tam_original > 0) {
        fprintf(stderr, "Error: tabla de frecuencias invalida o sin memoria\n");
        goto fin;
    }

    salida = malloc(tam_original > 0 ? (size_t)tam_original : 1);
    if (salida == NULL) {
        perror("malloc");
        goto fin;
    }

    /* 5. Decodificar todos los bloques en paralelo. */
    d.raiz         = raiz;
    d.datos        = datos;
    d.tamanos      = tamanos;
    d.inicio       = inicio;
    d.salida       = salida;
    d.tam_original = tam_original;
    d.tam_bloque   = tam_bloque;
    d.progreso     = progreso;

    huff_progreso_fijar_total(progreso, n_bloques);
    if (pool_ejecutar(n_hilos, n_bloques, tarea_decodificar, &d) < 0) {
        goto fin;
    }

    /* 6. Escribir el resultado. */
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
    if (fd >= 0) {
        close(fd);
        if (resultado < 0) {
            unlink(ruta_salida);   /* no se deja un archivo a medias */
        }
    }
    free(salida);
    free(inicio);
    free(tamanos);
    free(archivo);
    liberar_arbol(raiz);
    huff_progreso_terminar(progreso);
    return resultado;
}
