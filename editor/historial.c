/* Guarda versiones del archivo para poder deshacer y rehacer cambios. */

#include "editor.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/* Copia y restauracion de versiones temporales. */

static int copiar_a_swap(Editor *ed, const char *ruta)
{
    int fd_dst = open(ruta, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd_dst == -1) {
        perror("open (swap)");
        return -1;
    }

    if (lseek(ed->fd, 0, SEEK_SET) == -1) {
        perror("lseek");
        close(fd_dst);
        return -1;
    }

    char    bloque[ED_BLOQUE];
    ssize_t leidos;

    while ((leidos = read(ed->fd, bloque, sizeof(bloque))) > 0) {
        if (escribir_todo(fd_dst, bloque, (size_t)leidos) == -1) {
            close(fd_dst);
            return -1;
        }
    }

    if (leidos == -1) {
        perror("read");
        close(fd_dst);
        return -1;
    }

    close(fd_dst);
    return 0;
}

static int restaurar_desde_swap(Editor *ed, const char *ruta)
{
    int fd_src = open(ruta, O_RDONLY);
    if (fd_src == -1) {
        perror("open (swap)");
        return -1;
    }

    if (lseek(ed->fd, 0, SEEK_SET) == -1) {
        perror("lseek");
        close(fd_src);
        return -1;
    }

    char    bloque[ED_BLOQUE];
    off_t   total = 0;
    ssize_t leidos;

    while ((leidos = read(fd_src, bloque, sizeof(bloque))) > 0) {
        if (escribir_todo(ed->fd, bloque, (size_t)leidos) == -1) {
            close(fd_src);
            return -1;
        }
        total += leidos;
    }

    if (leidos == -1) {
        perror("read");
        close(fd_src);
        return -1;
    }

    close(fd_src);

    if (ftruncate(ed->fd, total) == -1) {
        perror("ftruncate");
        return -1;
    }

    return ed_indexar(ed);
}

static void descartar_desde(Editor *ed, int desde)
{
    for (int i = desde; i < ed->hist.n; i++) {
        unlink(ed->hist.rutas[i]);
    }
    ed->hist.n = desde;
}

/* Manejo del historial de cambios. */

void hist_init(Editor *ed)
{
    ed->hist.n        = 0;
    ed->hist.actual   = -1;
    ed->hist.contador = 0;
}

int hist_registrar(Editor *ed)
{
    if (!ed_esta_abierto(ed)) return -1;

    if (ed->hist.actual >= 0 && ed->hist.actual + 1 < ed->hist.n) {
        descartar_desde(ed, ed->hist.actual + 1);
    }

    if (ed->hist.n == ED_MAX_VERSIONES) {
        unlink(ed->hist.rutas[0]);
        for (int i = 1; i < ed->hist.n; i++) {
            strcpy(ed->hist.rutas[i - 1], ed->hist.rutas[i]);
        }
        ed->hist.n--;
        ed->hist.actual--;
    }

    char ruta[ED_MAX_RUTA];
    snprintf(ruta, sizeof(ruta), "/tmp/editor_%d_%d.swap",
             (int)getpid(), ed->hist.contador);

    if (copiar_a_swap(ed, ruta) == -1) return -1;

    memcpy(ed->hist.rutas[ed->hist.n], ruta, sizeof(ruta));

    ed->hist.n++;
    ed->hist.actual = ed->hist.n - 1;
    ed->hist.contador++;

    return 0;
}

int hist_deshacer(Editor *ed)
{
    if (!ed_esta_abierto(ed)) return -1;
    if (ed->hist.actual <= 0) return 1;

    if (restaurar_desde_swap(ed, ed->hist.rutas[ed->hist.actual - 1]) == -1) {
        return -1;
    }

    ed->hist.actual--;
    return 0;
}

int hist_rehacer(Editor *ed)
{
    if (!ed_esta_abierto(ed)) return -1;
    if (ed->hist.actual < 0)                return 1;
    if (ed->hist.actual + 1 >= ed->hist.n)  return 1;

    if (restaurar_desde_swap(ed, ed->hist.rutas[ed->hist.actual + 1]) == -1) {
        return -1;
    }

    ed->hist.actual++;
    return 0;
}

void hist_limpiar(Editor *ed)
{
    for (int i = 0; i < ed->hist.n; i++) {
        unlink(ed->hist.rutas[i]);
    }
    hist_init(ed);
}

int hist_reemplazar(Editor *ed, const char *ruta)
{
    if (!ed_esta_abierto(ed)) return -1;

    if (restaurar_desde_swap(ed, ruta) == -1) return -1;

    return hist_registrar(ed);
}
