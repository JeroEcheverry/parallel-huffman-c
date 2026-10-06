/*
 * main_huff.c -- programa de prueba del compresor, sin el editor.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Uso:
 *   ./huff c <entrada> <salida.huff>   comprimir
 *   ./huff d <entrada.huff> <salida>   descomprimir
 *
 * Permite probar el modulo de Huffman por separado antes de integrarlo
 * al editor del parcial 1.
 */

#include "huffman.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 4 || (strcmp(argv[1], "c") != 0 && strcmp(argv[1], "d") != 0)) {
        fprintf(stderr, "Uso: %s c|d <entrada> <salida>\n", argv[0]);
        return 1;
    }

    int resultado;
    if (strcmp(argv[1], "c") == 0) {
        resultado = huff_comprimir(argv[2], argv[3]);
    } else {
        resultado = huff_descomprimir(argv[2], argv[3]);
    }

    if (resultado != 0) {
        fprintf(stderr, "Error al procesar '%s'\n", argv[2]);
        return 1;
    }
    return 0;
}