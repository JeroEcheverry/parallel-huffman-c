/*
 * edicion.c -- operaciones que modifican el archivo: anadir, insertar,
 * borrar. El kernel no tiene una syscall que inserte o quite bytes en
 * medio de un archivo, asi que hay que desplazar a mano los bytes que
 * quedan despues y ajustar el tamano con ftruncate.
 */

#include "editor.h"

#include <unistd.h>     /* read, write, lseek, ftruncate */
#include <stdio.h>      /* perror                        */
#include <stdlib.h>     /* malloc, free                  */
#include <string.h>     /* memcpy, strlen                */

/* ---------------------------------------------------------------- */
/* a <texto>  --  anadir una linea al final                           */
/* ---------------------------------------------------------------- */

/*
 * Anade 'texto' como nueva linea al final. Si el archivo no termina en
 * '\n' se antepone uno para que el texto no quede pegado a la ultima linea.
 */
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

/* ---------------------------------------------------------------- */
/* i <n> <texto>  --  insertar una linea en una posicion arbitraria    */
/* ---------------------------------------------------------------- */

/*
 * Inserta 'texto' como nueva linea en 'idx' (base 0), desplazando hacia
 * adelante esa linea y las siguientes.
 *
 * Si 'idx' es igual al numero de lineas, equivale a anadir al final y se
 * delega en ed_anexar.
 *
 * Para abrir el hueco, la region [off, tam) se copia hacia adelante desde
 * el final hacia el principio, porque origen y destino se solapan (igual
 * razon por la que memmove existe junto a memcpy). No hace falta
 * ftruncate: el archivo crece solo al escribir mas alla de su ultimo byte.
 */
int ed_insertar(Editor *ed, size_t idx, const char *texto)
{
    if (!ed_esta_abierto(ed)) return -1;
    if (idx > ed->n_lineas)   return -1;

    if (idx == ed->n_lineas) {
        return ed_anexar(ed, texto);
    }

    off_t  off   = ed->lineas[idx].offset;
    size_t largo = strlen(texto);
    size_t hueco = largo + 1;   /* texto + '\n' */

    /* desplazar la cola hacia adelante, de atras hacia adelante */

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

    /* escribir la linea nueva en el hueco */

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

/* ---------------------------------------------------------------- */
/* d <n>  --  borrar una linea                                        */
/* ---------------------------------------------------------------- */

/*
 * Borra la linea 'idx' (base 0): desplaza los bytes posteriores hacia
 * atras (sobrescribiendo la linea eliminada) y recorta el archivo con
 * ftruncate. Aqui el destino queda detras del origen, asi que el
 * recorrido va de principio a fin (direccion opuesta a ed_insertar).
 */
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

    /* recorta la copia sobrante del final */
    off_t nuevo_tam = ed->tam - (off_t)largo;
    if (ftruncate(ed->fd, nuevo_tam) == -1) {
        perror("ftruncate");
        return -1;
    }

    return ed_indexar(ed);
}
