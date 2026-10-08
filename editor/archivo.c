/* Apertura, lectura e indice de lineas del archivo. */

#include "editor.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Lectura y escritura del archivo. */

ssize_t leer_exacto(int fd, void *buf, size_t n)
{
    size_t total = 0;
    char  *p     = (char *)buf;

    while (total < n) {
        ssize_t r = read(fd, p + total, n - total);

        if (r < 0) {
            if (errno == EINTR) continue;
            perror("read");
            return -1;
        }
        if (r == 0) break;

        total += (size_t)r;
    }
    return (ssize_t)total;
}

ssize_t escribir_todo(int fd, const void *buf, size_t n)
{
    size_t      total = 0;
    const char *p     = (const char *)buf;

    while (total < n) {
        ssize_t w = write(fd, p + total, n - total);

        if (w < 0) {
            if (errno == EINTR) continue;
            perror("write");
            return -1;
        }
        total += (size_t)w;
    }
    return (ssize_t)total;
}

/* Preparacion del editor y manejo del archivo abierto. */

void ed_init(Editor *ed)
{
    ed->fd       = -1;
    ed->ruta[0]  = '\0';
    ed->lineas   = NULL;
    ed->n_lineas = 0;
    ed->cap      = 0;
    ed->tam      = 0;

    ed->portapapeles       = NULL;
    ed->portapapeles_largo = 0;

    hist_init(ed);
}

int ed_esta_abierto(const Editor *ed)
{
    return ed->fd >= 0;
}

int ed_abrir(Editor *ed, const char *ruta)
{
    if (ed_esta_abierto(ed)) {
        ed_cerrar(ed);
    }

    int fd = open(ruta, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        perror("open");
        return -1;
    }

    ed->fd = fd;
    strncpy(ed->ruta, ruta, ED_MAX_RUTA - 1);
    ed->ruta[ED_MAX_RUTA - 1] = '\0';

    ed->lineas = malloc(ED_CAP_INICIAL * sizeof(Linea));
    if (ed->lineas == NULL) {
        perror("malloc");
        close(fd);
        ed->fd = -1;
        return -1;
    }
    ed->cap      = ED_CAP_INICIAL;
    ed->n_lineas = 0;

    if (ed_indexar(ed) == -1) {
        ed_cerrar(ed);
        return -1;
    }

    return 0;
}

int ed_cerrar(Editor *ed)
{
    int r = 0;

    hist_limpiar(ed);
    ed_portapapeles_liberar(ed);

    if (ed->fd >= 0) {
        if (close(ed->fd) == -1) {
            perror("close");
            r = -1;
        }
        ed->fd = -1;
    }

    free(ed->lineas);
    ed->lineas   = NULL;
    ed->n_lineas = 0;
    ed->cap      = 0;
    ed->tam      = 0;
    ed->ruta[0]  = '\0';

    return r;
}

/* Indice con la posicion y el largo de cada linea. */
static int indice_agregar(Editor *ed, off_t offset, size_t largo)
{
    if (ed->n_lineas == ed->cap) {
        size_t nueva_cap = (ed->cap == 0) ? ED_CAP_INICIAL : ed->cap * 2;

        Linea *tmp = realloc(ed->lineas, nueva_cap * sizeof(Linea));
        if (tmp == NULL) {
            perror("realloc");
            return -1;
        }
        ed->lineas = tmp;
        ed->cap    = nueva_cap;
    }

    ed->lineas[ed->n_lineas].offset = offset;
    ed->lineas[ed->n_lineas].largo  = largo;
    ed->n_lineas++;
    return 0;
}

int ed_indexar(Editor *ed)
{
    if (!ed_esta_abierto(ed)) return -1;

    off_t tam = lseek(ed->fd, 0, SEEK_END);
    if (tam == -1) { perror("lseek"); return -1; }
    ed->tam = tam;

    if (lseek(ed->fd, 0, SEEK_SET) == -1) { perror("lseek"); return -1; }

    ed->n_lineas = 0;

    char    bloque[ED_BLOQUE];
    off_t   pos    = 0;
    off_t   inicio = 0;
    ssize_t leidos;

    while ((leidos = read(ed->fd, bloque, sizeof(bloque))) > 0) {
        for (ssize_t i = 0; i < leidos; i++) {
            if (bloque[i] == '\n') {
                off_t  fin_linea = pos + i;
                size_t largo     = (size_t)(fin_linea - inicio + 1);

                if (indice_agregar(ed, inicio, largo) == -1) return -1;
                inicio = fin_linea + 1;
            }
        }
        pos += leidos;
    }

    if (leidos == -1) { perror("read"); return -1; }

    if (inicio < ed->tam) {
        if (indice_agregar(ed, inicio, (size_t)(ed->tam - inicio)) == -1) return -1;
    }

    return 0;
}

/* Mostrar y copiar lineas del archivo. */
int ed_imprimir_linea(Editor *ed, size_t idx)
{
    if (!ed_esta_abierto(ed)) return -1;
    if (idx >= ed->n_lineas)  return -1;

    size_t restante        = ed->lineas[idx].largo;
    int    termina_en_salto = 0;
    char   bloque[ED_BLOQUE];

    fflush(stdout);

    if (lseek(ed->fd, ed->lineas[idx].offset, SEEK_SET) == -1) {
        perror("lseek");
        return -1;
    }

    while (restante > 0) {
        size_t pedir = (restante > ED_BLOQUE) ? ED_BLOQUE : restante;

        ssize_t leidos = leer_exacto(ed->fd, bloque, pedir);
        if (leidos <= 0) break;

        if (escribir_todo(1, bloque, (size_t)leidos) == -1) return -1;

        termina_en_salto = (bloque[leidos - 1] == '\n');
        restante -= (size_t)leidos;
    }

    if (!termina_en_salto) {
        if (escribir_todo(1, "\n", 1) == -1) return -1;
    }

    return 0;
}

char *ed_linea_a_memoria(Editor *ed, size_t idx, size_t *largo_out)
{
    if (!ed_esta_abierto(ed)) return NULL;
    if (idx >= ed->n_lineas)  return NULL;

    size_t largo = ed->lineas[idx].largo;

    char *buf = malloc(largo + 1);
    if (buf == NULL) {
        perror("malloc");
        return NULL;
    }

    if (lseek(ed->fd, ed->lineas[idx].offset, SEEK_SET) == -1) {
        perror("lseek");
        free(buf);
        return NULL;
    }
    if (leer_exacto(ed->fd, buf, largo) != (ssize_t)largo) {
        free(buf);
        return NULL;
    }

    if (largo > 0 && buf[largo - 1] == '\n') largo--;

    buf[largo] = '\0';
    if (largo_out) *largo_out = largo;

    return buf;
}
