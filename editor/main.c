/* Inicio del editor cuando se ejecuta como programa independiente. */

#include "editor.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "Uso: %s [archivo]\n", argv[0]);
        return 1;
    }

    printf("=========================================\n");
    printf("  Editor de texto CLI + compresor Huffman\n");
    printf("  concurrente  -  SO2026B (EAFIT)\n");
    printf("  Escribe 'h' para ver la ayuda.\n");
    printf("=========================================\n");

    const char *ruta_inicial = (argc == 2) ? argv[1] : NULL;

    return editor_ejecutar(ruta_inicial);
}
