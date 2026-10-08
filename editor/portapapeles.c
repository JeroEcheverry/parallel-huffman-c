/*
 * portapapeles.c -- comandos 'y' (copiar) y 'x' (pegar).
 *
 * El portapapeles es un buffer (malloc) dentro de la struct Editor que
 * guarda una sola linea, igual que el registro de borrado de vi. Se
 * conserva entre comandos y se libera al cerrar el archivo.
 */

#include "editor.h"

#include <stdio.h>    /* printf */
#include <stdlib.h>   /* free   */

/* Libera el portapapeles y lo deja vacio. Seguro de llamar si ya lo esta. */
void ed_portapapeles_liberar(Editor *ed)
{
    free(ed->portapapeles);
    ed->portapapeles       = NULL;
    ed->portapapeles_largo = 0;
}

/* ---------------------------------------------------------------- */
/* y <n>  --  copiar la linea n al portapapeles                       */
/* ---------------------------------------------------------------- */

/*
 * Copia la linea 'idx' (base 0) al portapapeles, reemplazando lo que
 * hubiera antes. Se guarda sin el '\n' final, que ed_insertar vuelve a
 * poner al pegar.
 */
int ed_copiar(Editor *ed, size_t idx)
{
    if (!ed_esta_abierto(ed)) return -1;
    if (idx >= ed->n_lineas)  return -1;

    size_t largo = 0;
    char  *texto = ed_linea_a_memoria(ed, idx, &largo);
    if (texto == NULL) return -1;

    ed_portapapeles_liberar(ed);

    ed->portapapeles       = texto;
    ed->portapapeles_largo = largo;

    return 0;
}

/* ---------------------------------------------------------------- */
/* x <n>  --  pegar el portapapeles como nueva linea n                */
/* ---------------------------------------------------------------- */

/*
 * Inserta el contenido del portapapeles como nueva linea en 'idx'
 * (base 0). Se delega en ed_insertar; el portapapeles no se vacia, asi
 * que la misma linea se puede pegar varias veces.
 */
int ed_pegar(Editor *ed, size_t idx)
{
    if (!ed_esta_abierto(ed)) return -1;

    if (ed->portapapeles == NULL) {
        printf("Error: el portapapeles esta vacio. Usa primero: y <n>\n");
        return -1;
    }

    return ed_insertar(ed, idx, ed->portapapeles);
}
