/**
 * ====================================================================================
 *  main.c  --  Punto de entrada del editor como programa independiente
 * ====================================================================================
 *  Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 1
 *
 *  Este archivo solo se compila cuando el editor se construye como binario propio
 *  (./editor). Cuando el editor se integra dentro del shell eafitOS, main.c se
 *  excluye de la compilacion y el bucle se invoca desde cat_edicion.c, porque el
 *  shell ya tiene su propia funcion main.
 *
 *  Toda la logica esta en repl.c: aqui solo se traduce el argumento de la linea de
 *  comandos y se cede el control.
 * ====================================================================================
 */

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

    /* Si se paso un archivo se abre de entrada; si no, se arranca sin archivo. */
    const char *ruta_inicial = (argc == 2) ? argv[1] : NULL;

    return editor_ejecutar(ruta_inicial);
}
