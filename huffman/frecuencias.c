/* Cuenta cuantas veces aparece cada byte en un bloque. */
#include "huffman.h"

void contar_frecuencias(const unsigned char *buf, size_t n,
                        uint64_t freq[HUFF_SIMBOLOS])
{
    for (size_t i = 0; i < n; i++) {
        freq[buf[i]]++;
    }
}
