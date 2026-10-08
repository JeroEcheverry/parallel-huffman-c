/*
 * busqueda.c -- comandos 's' (buscar palabra) y 'm' (metadatos).
 */

#include "editor.h"

#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Busqueda de texto en las lineas del archivo. */
int ed_buscar(Editor *ed, const char *palabra)
{
    if (!ed_esta_abierto(ed)) return -1;
    if (palabra == NULL || *palabra == '\0') return -1;

    int encontradas = 0;

    for (size_t i = 0; i < ed->n_lineas; i++) {
        char *texto = ed_linea_a_memoria(ed, i, NULL);
        if (texto == NULL) return -1;

        if (strstr(texto, palabra) != NULL) {
            printf("%4zu | %s\n", i + 1, texto);
            encontradas++;
        }

        free(texto);
    }

    return encontradas;
}

/* Convierte los permisos del archivo a texto. */
static void permisos_a_texto(mode_t modo, char *salida)
{
    salida[0] = (modo & S_IRUSR) ? 'r' : '-';
    salida[1] = (modo & S_IWUSR) ? 'w' : '-';
    salida[2] = (modo & S_IXUSR) ? 'x' : '-';
    salida[3] = (modo & S_IRGRP) ? 'r' : '-';
    salida[4] = (modo & S_IWGRP) ? 'w' : '-';
    salida[5] = (modo & S_IXGRP) ? 'x' : '-';
    salida[6] = (modo & S_IROTH) ? 'r' : '-';
    salida[7] = (modo & S_IWOTH) ? 'w' : '-';
    salida[8] = (modo & S_IXOTH) ? 'x' : '-';
    salida[9] = '\0';
}

/* Muestra informacion del archivo abierto. */
int ed_metadatos(Editor *ed)
{
    if (!ed_esta_abierto(ed)) return -1;

    struct stat st;

    if (fstat(ed->fd, &st) == -1) {
        perror("fstat");
        return -1;
    }

    char permisos[10];
    permisos_a_texto(st.st_mode, permisos);

    char fecha[64];
    struct tm *t = localtime(&st.st_mtime);
    if (t != NULL) {
        strftime(fecha, sizeof(fecha), "%Y-%m-%d %H:%M:%S", t);
    } else {
        fecha[0] = '\0';
    }

    printf("Archivo:       %s\n",        ed->ruta);
    printf("Tamano:        %ld bytes\n", (long)st.st_size);
    printf("Lineas:        %zu\n",       ed->n_lineas);
    printf("Permisos:      %s (%o)\n",   permisos, st.st_mode & 07777);
    printf("Inodo:         %lu\n",       (unsigned long)st.st_ino);
    printf("Enlaces:       %lu\n",       (unsigned long)st.st_nlink);
    printf("Modificado:    %s\n",        fecha);
    printf("Descriptor:    %d\n",        ed->fd);

    return 0;
}
