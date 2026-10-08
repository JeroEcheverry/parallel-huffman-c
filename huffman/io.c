/*
 * io.c -- lectura y escritura de archivos con llamadas al sistema POSIX.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * read y write pueden transferir menos bytes de los pedidos (por ejemplo
 * si llega una senal). Por eso se repiten en un ciclo hasta completar.
 * Las funciones llevan el prefijo huff_ para no chocar con leer_exacto y
 * escribir_todo del editor cuando ambos modulos se enlacen juntos.
 */
#include "huffman.h"

#include <errno.h>     /* errno, EINTR        */
#include <fcntl.h>     /* open, O_RDONLY      */
#include <stdio.h>     /* perror              */
#include <stdlib.h>    /* malloc, free        */
#include <sys/stat.h>  /* fstat, struct stat  */
#include <unistd.h>    /* read, write, close  */

/* Lee exactamente n bytes de fd. Retorna n, o -1 si hubo error o EOF antes. */
static ssize_t leer_exacto(int fd, void *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, (char *)buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;   /* interrumpido por una senal: reintentar */
            return -1;
        }
        if (r == 0) {
            return -1;                      /* el archivo termino antes de lo esperado */
        }
        total += (size_t)r;
    }
    return (ssize_t)total;
}

ssize_t huff_escribir_todo(int fd, const void *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t w = write(fd, (const char *)buf + total, n - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += (size_t)w;
    }
    return (ssize_t)total;
}

int huff_leer_archivo(const char *ruta, unsigned char **buf, size_t *n)
{
    int fd = open(ruta, O_RDONLY);
    if (fd < 0) {
        perror(ruta);
        return -1;
    }

    /* fstat da el tamano del archivo para reservar la memoria justa. */
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return -1;
    }
    size_t tam = (size_t)st.st_size;

    /* malloc(0) puede retornar NULL; se reserva al menos 1 byte. */
    unsigned char *datos = malloc(tam > 0 ? tam : 1);
    if (datos == NULL) {
        perror("malloc");
        close(fd);
        return -1;
    }

    if (leer_exacto(fd, datos, tam) < 0) {
        perror("read");
        free(datos);
        close(fd);
        return -1;
    }

    close(fd);
    *buf = datos;
    *n   = tam;
    return 0;
}
