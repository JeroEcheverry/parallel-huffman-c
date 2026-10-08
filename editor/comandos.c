/*
 * comandos.c -- capa de comandos: valida lo que escribe el usuario,
 * convierte la numeracion de lineas (1, 2, 3...) a indices (0, 1, 2...)
 * y llama a las funciones de archivo.c / edicion.c.
 */

#include "editor.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Validaciones usadas por los comandos. */
static long parsear_numero(const char *txt)
{
    if (txt == NULL || *txt == '\0') return -1;

    char *fin = NULL;
    errno = 0;
    long v = strtol(txt, &fin, 10);

    if (errno != 0)   return -1;
    if (fin == txt)   return -1;

    while (*fin == ' ' || *fin == '\t') fin++;

    if (*fin != '\0') return -1;
    if (v < 1)        return -1;

    return v;
}

static int exigir_archivo(const Editor *ed)
{
    if (!ed_esta_abierto(ed)) {
        printf("Error: no hay ningun archivo abierto. Usa: o <archivo>\n");
        return -1;
    }
    return 0;
}

/* Abrir y mostrar el contenido del archivo. */
int cmd_o(Editor *ed, const char *arg)
{
    if (arg == NULL || *arg == '\0') {
        printf("Uso: o <archivo>\n");
        return -1;
    }

    if (ed_abrir(ed, arg) == -1) {
        return -1;
    }

    hist_registrar(ed);

    printf("Archivo '%s' abierto (fd=%d, %ld bytes, %zu lineas).\n",
           ed->ruta, ed->fd, (long)ed->tam, ed->n_lineas);
    return 0;
}

int cmd_p(Editor *ed, const char *arg)
{
    if (exigir_archivo(ed) == -1) return -1;

    if (arg == NULL || *arg == '\0') {
        if (ed->n_lineas == 0) {
            printf("(archivo vacio)\n");
            return 0;
        }

        for (size_t i = 0; i < ed->n_lineas; i++) {
            printf("%4zu | ", i + 1);
            if (ed_imprimir_linea(ed, i) == -1) return -1;
        }
        return 0;
    }

    long n_linea = parsear_numero(arg);
    if (n_linea < 0) {
        printf("Uso: p [n]   (n debe ser un entero mayor o igual a 1)\n");
        return -1;
    }
    if ((size_t)n_linea > ed->n_lineas) {
        printf("Error: la linea %ld no existe (el archivo tiene %zu lineas).\n",
               n_linea, ed->n_lineas);
        return -1;
    }

    return ed_imprimir_linea(ed, (size_t)n_linea - 1);
}

/* Modificar el archivo. */
int cmd_a(Editor *ed, const char *arg)
{
    if (exigir_archivo(ed) == -1) return -1;

    if (arg == NULL) arg = "";

    if (ed_anexar(ed, arg) == -1) return -1;
    hist_registrar(ed);

    printf("Linea anadida. El archivo tiene ahora %zu lineas (%ld bytes).\n",
           ed->n_lineas, (long)ed->tam);
    return 0;
}

int cmd_d(Editor *ed, const char *arg)
{
    if (exigir_archivo(ed) == -1) return -1;

    long n_linea = parsear_numero(arg);
    if (n_linea < 0) {
        printf("Uso: d <n>   (n debe ser un entero mayor o igual a 1)\n");
        return -1;
    }
    if (ed->n_lineas == 0) {
        printf("Error: el archivo esta vacio, no hay nada que borrar.\n");
        return -1;
    }
    if ((size_t)n_linea > ed->n_lineas) {
        printf("Error: la linea %ld no existe (el archivo tiene %zu lineas).\n",
               n_linea, ed->n_lineas);
        return -1;
    }

    if (ed_borrar_linea(ed, (size_t)n_linea - 1) == -1) return -1;
    hist_registrar(ed);

    printf("Linea %ld borrada. Quedan %zu lineas (%ld bytes).\n",
           n_linea, ed->n_lineas, (long)ed->tam);
    return 0;
}

int cmd_i(Editor *ed, const char *arg)
{
    if (exigir_archivo(ed) == -1) return -1;

    if (arg == NULL || *arg == '\0') {
        printf("Uso: i <n> <texto>\n");
        return -1;
    }

    char *fin = NULL;
    errno = 0;
    long n_linea = strtol(arg, &fin, 10);

    if (fin == arg || errno != 0 || n_linea < 1) {
        printf("Uso: i <n> <texto>   (n debe ser un entero mayor o igual a 1)\n");
        return -1;
    }

    if ((size_t)n_linea > ed->n_lineas + 1) {
        printf("Error: no se puede insertar en la linea %ld (el archivo tiene %zu lineas).\n",
               n_linea, ed->n_lineas);
        return -1;
    }

    while (*fin == ' ' || *fin == '\t') fin++;

    if (ed_insertar(ed, (size_t)n_linea - 1, fin) == -1) return -1;
    hist_registrar(ed);

    printf("Texto insertado como linea %ld. El archivo tiene ahora %zu lineas (%ld bytes).\n",
           n_linea, ed->n_lineas, (long)ed->tam);
    return 0;
}

/* Buscar texto y mostrar los datos del archivo. */
int cmd_s(Editor *ed, const char *arg)
{
    if (exigir_archivo(ed) == -1) return -1;

    if (arg == NULL || *arg == '\0') {
        printf("Uso: s <palabra>\n");
        return -1;
    }

    int encontradas = ed_buscar(ed, arg);
    if (encontradas == -1) return -1;

    if (encontradas == 0) {
        printf("La palabra '%s' no aparece en el archivo.\n", arg);
    } else {
        printf("%d linea(s) contienen '%s'.\n", encontradas, arg);
    }
    return 0;
}

int cmd_m(Editor *ed, const char *arg)
{
    (void)arg;

    if (exigir_archivo(ed) == -1) return -1;

    return ed_metadatos(ed);
}

/* Copiar y pegar lineas. */
int cmd_y(Editor *ed, const char *arg)
{
    if (exigir_archivo(ed) == -1) return -1;

    long n_linea = parsear_numero(arg);
    if (n_linea < 0) {
        printf("Uso: y <n>   (n debe ser un entero mayor o igual a 1)\n");
        return -1;
    }
    if ((size_t)n_linea > ed->n_lineas) {
        printf("Error: la linea %ld no existe (el archivo tiene %zu lineas).\n",
               n_linea, ed->n_lineas);
        return -1;
    }

    if (ed_copiar(ed, (size_t)n_linea - 1) == -1) return -1;

    printf("Linea %ld copiada al portapapeles (%zu bytes).\n",
           n_linea, ed->portapapeles_largo);
    return 0;
}

int cmd_x(Editor *ed, const char *arg)
{
    if (exigir_archivo(ed) == -1) return -1;

    long n_linea = parsear_numero(arg);
    if (n_linea < 0) {
        printf("Uso: x <n>   (n debe ser un entero mayor o igual a 1)\n");
        return -1;
    }
    if ((size_t)n_linea > ed->n_lineas + 1) {
        printf("Error: no se puede pegar en la linea %ld (el archivo tiene %zu lineas).\n",
               n_linea, ed->n_lineas);
        return -1;
    }

    if (ed_pegar(ed, (size_t)n_linea - 1) == -1) return -1;
    hist_registrar(ed);

    printf("Portapapeles pegado como linea %ld. El archivo tiene ahora %zu lineas.\n",
           n_linea, ed->n_lineas);
    return 0;
}

/* Deshacer y rehacer cambios. */
int cmd_u(Editor *ed, const char *arg)
{
    (void)arg;

    if (exigir_archivo(ed) == -1) return -1;

    int r = hist_deshacer(ed);
    if (r == -1) return -1;

    if (r == 1) {
        printf("No hay nada mas que deshacer.\n");
        return 0;
    }

    printf("Deshecho. El archivo tiene ahora %zu lineas (%ld bytes).\n",
           ed->n_lineas, (long)ed->tam);
    return 0;
}

int cmd_r(Editor *ed, const char *arg)
{
    (void)arg;

    if (exigir_archivo(ed) == -1) return -1;

    int r = hist_rehacer(ed);
    if (r == -1) return -1;

    if (r == 1) {
        printf("No hay nada que rehacer.\n");
        return 0;
    }

    printf("Rehecho. El archivo tiene ahora %zu lineas (%ld bytes).\n",
           ed->n_lineas, (long)ed->tam);
    return 0;
}
