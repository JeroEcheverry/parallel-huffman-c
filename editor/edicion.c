/* Funciones para agregar, insertar y borrar lineas del archivo. */

#include "editor.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Agregar una linea al final del archivo. */
int ed_anexar(Editor *ed, const char *texto)
{
    if (!ed_esta_abierto(ed)) return -1;

    size_t largo       = strlen(texto);
    int    falta_salto = 0;

    if (ed->tam > 0) {
        char ultimo;
        if (lseek(ed->fd, -1, SEEK_END) == -1) { perror("lseek"); return -1; }
        if (leer_exacto(ed->fd, &ultimo, 1) != 1) return -1;
        if (ultimo != '\n') falta_salto = 1;
    }

    size_t n_total = largo + 1 + (falta_salto ? 1 : 0);

    char *buf = malloc(n_total);
    if (buf == NULL) {
        perror("malloc");
        return -1;
    }

    size_t n = 0;
    if (falta_salto) buf[n++] = '\n';
    memcpy(buf + n, texto, largo);
    n += largo;
    buf[n++] = '\n';

    if (lseek(ed->fd, 0, SEEK_END) == -1) {
        perror("lseek");
        free(buf);
        return -1;
    }
    if (escribir_todo(ed->fd, buf, n) == -1) {
        free(buf);
        return -1;
    }

    free(buf);

    return ed_indexar(ed);
}

/* Insertar una linea en la posicion indicada. */
int ed_insertar(Editor *ed, size_t idx, const char *texto)
{
    if (!ed_esta_abierto(ed)) return -1;
    if (idx > ed->n_lineas)   return -1;

    if (idx == ed->n_lineas) {
        return ed_anexar(ed, texto);
    }

    off_t  off   = ed->lineas[idx].offset;
    size_t largo = strlen(texto);
    size_t hueco = largo + 1;

    char  bloque[ED_BLOQUE];
    off_t fin = ed->tam;

    while (fin > off) {
        off_t  restante = fin - off;
        size_t pedir    = (restante > ED_BLOQUE) ? ED_BLOQUE : (size_t)restante;

        off_t src = fin - (off_t)pedir;

        if (lseek(ed->fd, src, SEEK_SET) == -1) { perror("lseek"); return -1; }
        if (leer_exacto(ed->fd, bloque, pedir) != (ssize_t)pedir) return -1;

        if (lseek(ed->fd, src + (off_t)hueco, SEEK_SET) == -1) { perror("lseek"); return -1; }
        if (escribir_todo(ed->fd, bloque, pedir) == -1) return -1;

        fin = src;
    }

    char *buf = malloc(hueco);
    if (buf == NULL) {
        perror("malloc");
        return -1;
    }
    memcpy(buf, texto, largo);
    buf[largo] = '\n';

    if (lseek(ed->fd, off, SEEK_SET) == -1) {
        perror("lseek");
        free(buf);
        return -1;
    }
    if (escribir_todo(ed->fd, buf, hueco) == -1) {
        free(buf);
        return -1;
    }

    free(buf);

    return ed_indexar(ed);
}

/* Borrar una linea y actualizar el indice del archivo. */
int ed_borrar_linea(Editor *ed, size_t idx)
{
    if (!ed_esta_abierto(ed)) return -1;
    if (idx >= ed->n_lineas)  return -1;

    off_t  inicio = ed->lineas[idx].offset;
    size_t largo  = ed->lineas[idx].largo;

    off_t src = inicio + (off_t)largo;
    off_t dst = inicio;

    char bloque[ED_BLOQUE];

    while (src < ed->tam) {
        off_t  restante = ed->tam - src;
        size_t pedir    = (restante > ED_BLOQUE) ? ED_BLOQUE : (size_t)restante;

        if (lseek(ed->fd, src, SEEK_SET) == -1) { perror("lseek"); return -1; }
        ssize_t leidos = leer_exacto(ed->fd, bloque, pedir);
        if (leidos <= 0) break;

        if (lseek(ed->fd, dst, SEEK_SET) == -1) { perror("lseek"); return -1; }
        if (escribir_todo(ed->fd, bloque, (size_t)leidos) == -1) return -1;

        src += leidos;
        dst += leidos;
    }

    off_t nuevo_tam = ed->tam - (off_t)largo;
    if (ftruncate(ed->fd, nuevo_tam) == -1) {
        perror("ftruncate");
        return -1;
    }

    return ed_indexar(ed);
}
