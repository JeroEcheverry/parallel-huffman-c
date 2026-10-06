/*
 * io.c -- lectura y escritura de archivos completos con llamadas POSIX.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 */
#include "huffman.h"

#include <errno.h>     /* errno, EINTR                 */
#include <fcntl.h>     /* open, O_RDONLY               */
#include <stdio.h>     /* perror                       */
#include <stdlib.h>    /* malloc, free                 */
#include <sys/stat.h>  /* fstat                        */
#include <unistd.h>    /* read, write, close           */

int huff_leer_archivo(const char *ruta, unsigned char **buf, size_t *n)
{
    struct stat st;
    unsigned char *datos = NULL;
    size_t leidos = 0;

    int fd = open(ruta, O_RDONLY);
    if (fd < 0) {
        perror(ruta);
        return -1;
    }
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return -1;
    }

    /* Se reserva al menos 1 byte para que un archivo vacio no devuelva NULL. */
    size_t tam = (size_t)st.st_size;
    datos = malloc(tam > 0 ? tam : 1);
    if (datos == NULL) {
        perror("malloc");
        close(fd);
        return -1;
    }

    /* read() puede devolver menos de lo pedido: se repite hasta completar. */
    while (leidos < tam) {
        ssize_t r = read(fd, datos + leidos, tam - leidos);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("read");
            free(datos);
            close(fd);
            return -1;
        }
        if (r == 0) break;   /* el archivo se acorto mientras se leia */
        leidos += (size_t)r;
    }

    close(fd);
    *buf = datos;
    *n   = leidos;
    return 0;
}

int huff_escribir_todo(int fd, const void *buf, size_t n)
{
    const unsigned char *p = buf;
    size_t escritos = 0;

    while (escritos < n) {
        ssize_t w = write(fd, p + escritos, n - escritos);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        escritos += (size_t)w;
    }
    return 0;
}
