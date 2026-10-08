/* Funciones para copiar una linea y pegarla en otra posicion. */

#include "editor.h"

#include <stdio.h>
#include <stdlib.h>

/* Portapapeles: copiar y pegar lineas. */
void ed_portapapeles_liberar(Editor *ed)
{
    free(ed->portapapeles);
    ed->portapapeles       = NULL;
    ed->portapapeles_largo = 0;
}

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

int ed_pegar(Editor *ed, size_t idx)
{
    if (!ed_esta_abierto(ed)) return -1;

    if (ed->portapapeles == NULL) {
        printf("Error: el portapapeles esta vacio. Usa primero: y <n>\n");
        return -1;
    }

    return ed_insertar(ed, idx, ed->portapapeles);
}
