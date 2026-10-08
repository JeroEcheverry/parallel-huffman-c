/*
 * historial.c -- deshacer y rehacer con archivos de intercambio en /tmp.
 * Implementa los comandos 'u' (deshacer) y 'r' (rehacer).
 *
 * Cada vez que el archivo cambia se guarda una copia completa en /tmp
 * (una "version"). El historial es esa lista mas un indice 'actual' que
 * marca cual version corresponde al contenido real del archivo. Deshacer
 * retrocede el indice y restaura esa version; rehacer lo avanza. Si se
 * modifica el archivo despues de deshacer, las versiones que quedaban por
 * delante se descartan.
 *
 * Se usa /tmp y no memoria para poder deshacer sin depender de cuanto RAM
 * haya libre; el costo es guardar una copia completa por version.
 */

#include "editor.h"

#include <fcntl.h>      /* open, O_WRONLY, O_CREAT, O_TRUNC, O_RDONLY */
#include <unistd.h>     /* read, write, lseek, ftruncate, close, unlink, getpid */
#include <stdio.h>      /* printf, perror, snprintf                   */
#include <string.h>

/* ---------------------------------------------------------------- */
/* Utilidades internas                                                */
/* ---------------------------------------------------------------- */

/*
 * Copia el archivo abierto hacia 'ruta'. Permisos 0600 porque en /tmp
 * escriben todos los usuarios del sistema.
 */
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

/*
 * Copia el contenido de 'ruta' sobre el archivo que edita el usuario y
 * lo recorta con ftruncate al tamano de la version restaurada.
 */
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

/* Elimina del disco las versiones desde 'desde' en adelante. */
static void descartar_desde(Editor *ed, int desde)
{
    for (int i = desde; i < ed->hist.n; i++) {
        unlink(ed->hist.rutas[i]);
    }
    ed->hist.n = desde;
}

/* ---------------------------------------------------------------- */
/* Interfaz publica                                                   */
/* ---------------------------------------------------------------- */

void hist_init(Editor *ed)
{
    ed->hist.n        = 0;
    ed->hist.actual   = -1;
    ed->hist.contador = 0;
}

/*
 * Guarda el estado actual como version nueva. Se llama al abrir el
 * archivo y despues de cada modificacion exitosa. Si el historial esta
 * lleno se descarta la version mas antigua.
 */
int hist_registrar(Editor *ed)
{
    if (!ed_esta_abierto(ed)) return -1;

    /* una modificacion nueva invalida lo que quedaba por rehacer */
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

    /* el PID evita choques entre dos editores abiertos a la vez */
    char ruta[ED_MAX_RUTA];
    snprintf(ruta, sizeof(ruta), "/tmp/editor_%d_%d.swap",
             (int)getpid(), ed->hist.contador);

    if (copiar_a_swap(ed, ruta) == -1) return -1;

    /* Ambos arreglos miden ED_MAX_RUTA y 'ruta' ya termina en '\0'. */
    memcpy(ed->hist.rutas[ed->hist.n], ruta, sizeof(ruta));

    ed->hist.n++;
    ed->hist.actual = ed->hist.n - 1;
    ed->hist.contador++;

    return 0;
}

/* Restaura la version anterior. 0 si deshizo, 1 si no hay nada, -1 en error. */
int hist_deshacer(Editor *ed)
{
    if (!ed_esta_abierto(ed)) return -1;
    if (ed->hist.actual <= 0) return 1;   /* version 0 = estado inicial */

    if (restaurar_desde_swap(ed, ed->hist.rutas[ed->hist.actual - 1]) == -1) {
        return -1;
    }

    ed->hist.actual--;
    return 0;
}

/* Restaura la version siguiente. Mismos codigos de retorno que deshacer. */
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

/* Elimina todos los archivos temporales y vacia el historial. */
void hist_limpiar(Editor *ed)
{
    for (int i = 0; i < ed->hist.n; i++) {
        unlink(ed->hist.rutas[i]);
    }
    hist_init(ed);
}

/*
 * Reemplaza el contenido del archivo abierto por el de 'ruta' y lo
 * registra como version nueva del historial, para que el cambio se pueda
 * deshacer con 'u'. Lo usa la descompresion en segundo plano (fondo.c),
 * siempre con el cerrojo de escritor tomado.
 */
int hist_reemplazar(Editor *ed, const char *ruta)
{
    if (!ed_esta_abierto(ed)) return -1;

    if (restaurar_desde_swap(ed, ruta) == -1) return -1;

    return hist_registrar(ed);
}
