/*
 * main_huff.c -- programa de prueba del compresor, sin el editor.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Uso:
 *   ./huff c <entrada> <salida.huff> [hilos]   comprimir
 *   ./huff d <entrada.huff> <salida> [hilos]   descomprimir
 *
 * 'hilos' es opcional; si no se da se usa un hilo por nucleo.
 * Al terminar se imprime en stderr el tiempo que tomo la operacion,
 * para poder comparar el rendimiento con distinta cantidad de hilos.
 */

#include "huffman.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv)
{
    if ((argc != 4 && argc != 5) ||
        (strcmp(argv[1], "c") != 0 && strcmp(argv[1], "d") != 0)) {
        fprintf(stderr, "Uso: %s c|d <entrada> <salida> [hilos]\n", argv[0]);
        return 1;
    }

    int n_hilos = (argc == 5) ? atoi(argv[4]) : 0;
    if (n_hilos <= 0) n_hilos = huff_hilos_por_defecto();

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int resultado;
    if (strcmp(argv[1], "c") == 0) {
        resultado = huff_comprimir(argv[2], argv[3], n_hilos, NULL);
    } else {
        resultado = huff_descomprimir(argv[2], argv[3], n_hilos, NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0
              + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;

    if (resultado != 0) {
        fprintf(stderr, "Error al procesar '%s'\n", argv[2]);
        return 1;
    }

    fprintf(stderr, "%s con %d hilo(s): %.1f ms\n",
            argv[1][0] == 'c' ? "Compresion" : "Descompresion", n_hilos, ms);
    return 0;
}
