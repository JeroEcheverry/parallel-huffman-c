/* Funciones para leer y escribir archivos usando llamadas POSIX. */
#include "huffman.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* Lee o escribe todos los bytes solicitados. */
static ssize_t leer_exacto(int fd, void *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, (char *)buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) {
            return -1;
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

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return -1;
    }
    size_t tam = (size_t)st.st_size;

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
