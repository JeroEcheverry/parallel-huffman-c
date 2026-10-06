/*
 * frecuencias.c -- conteo de apariciones de cada byte.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 */
#include "huffman.h"

void contar_frecuencias(const unsigned char *buf, size_t n,
                        uint64_t freq[HUFF_SIMBOLOS])
{
    /* El valor de cada byte (0..255) se usa como indice en la tabla. */
    for (size_t i = 0; i < n; i++) {
        freq[buf[i]]++;
    }
}