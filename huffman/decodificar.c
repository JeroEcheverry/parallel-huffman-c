/* Lee un archivo .huff y recupera su contenido original. */
#include "huffman.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Datos compartidos por las tareas que decodifican los bloques. */
typedef struct {
    const Nodo          *raiz;
    const unsigned char *datos;
    const uint64_t      *tamanos;
    const uint64_t      *inicio;
    unsigned char       *salida;
    uint64_t             tam_original;
    uint64_t             tam_bloque;
    HuffProgreso        *progreso;
} Descompresion;

/* Decodifica un bloque siguiendo los bits por el arbol de Huffman. */
static int decodificar_datos(const Nodo *raiz,
                             const unsigned char *datos, size_t n_datos,
                             unsigned char *salida, uint64_t tam_original)
{
    uint64_t total_bits = (uint64_t)n_datos * 8;
    uint64_t pos = 0;

    /* Caso en que todos los bytes originales eran iguales. */
    if (raiz->izq == NULL && raiz->der == NULL) {
        if (total_bits < tam_original) return -1;
        memset(salida, raiz->simbolo, (size_t)tam_original);
        return 0;
    }

    for (uint64_t i = 0; i < tam_original; i++) {
        const Nodo *nodo = raiz;

        while (nodo->izq != NULL || nodo->der != NULL) {
            if (pos >= total_bits) return -1;

            int bit = (datos[pos / 8] >> (7 - (pos % 8))) & 1;
            pos++;

            nodo = bit ? nodo->der : nodo->izq;
        }
        salida[i] = (unsigned char)nodo->simbolo;
    }
    return 0;
}

/* Calcula la posicion de inicio de cada bloque comprimido. */
static void calcular_inicios(const uint64_t *tamanos, uint64_t n_bloques,
                             uint64_t *inicio)
{
    uint64_t acumulado = 0;

    for (uint64_t i = 0; i < n_bloques; i++) {
        inicio[i]  = acumulado;
        acumulado += tamanos[i];
    }
}

/* Tarea que decodifica un bloque para el pool de hilos. */
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
    const unsigned char *p     = NULL;
    const unsigned char *datos = NULL;
    size_t               n_datos = 0;
    Descompresion        d;

    /* Leer el archivo y validar su encabezado. */
    if (huff_leer_archivo(ruta_entrada, &archivo, &n) < 0) {
        goto fin;
    }

    if (n < HUFF_ENCABEZADO || memcmp(archivo, HUFF_FIRMA, HUFF_FIRMA_LARGO) != 0) {
        fprintf(stderr, "Error: '%s' no es un archivo .huff valido\n", ruta_entrada);
        goto fin;
    }
    p = archivo + HUFF_FIRMA_LARGO;
    memcpy(&tam_original, p, sizeof(uint64_t));  p += sizeof(uint64_t);
    memcpy(&tam_bloque,   p, sizeof(uint64_t));  p += sizeof(uint64_t);
    memcpy(&n_bloques,    p, sizeof(uint64_t));  p += sizeof(uint64_t);
    memcpy(freq, p, HUFF_SIMBOLOS * sizeof(uint64_t));

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

    /* Ubicar los bloques y comprobar que esten completos. */
    calcular_inicios(tamanos, n_bloques, inicio);
    for (uint64_t i = 0; i < n_bloques; i++) {
        if (tamanos[i] > n_datos || inicio[i] > n_datos - tamanos[i]) {
            fprintf(stderr, "Error: el bloque %llu se sale del archivo\n",
                    (unsigned long long)i);
            goto fin;
        }
    }

    /* Reconstruir el arbol usado durante la compresion. */
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

    /* Decodificar los bloques en paralelo. */
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

    /* Guardar el contenido recuperado. */
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
            unlink(ruta_salida);
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
