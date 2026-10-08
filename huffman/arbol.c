/* Construccion del arbol de Huffman y de los codigos para cada byte. */
#include "huffman.h"

#include <stdlib.h>
#include <string.h>

/* Funciones auxiliares para armar y recorrer el arbol. */
static Nodo *crear_nodo(int simbolo, uint64_t frecuencia, Nodo *izq, Nodo *der)
{
    Nodo *nodo = malloc(sizeof(Nodo));
    if (nodo == NULL) {
        return NULL;
    }
    nodo->simbolo    = simbolo;
    nodo->frecuencia = frecuencia;
    nodo->izq        = izq;
    nodo->der        = der;
    return nodo;
}

static Nodo *sacar_minimo(Nodo *nodos[], int *n)
{
    int min = 0;
    for (int i = 1; i < *n; i++) {
        if (nodos[i]->frecuencia < nodos[min]->frecuencia) {
            min = i;
        }
    }

    Nodo *resultado = nodos[min];

    for (int i = min; i < *n - 1; i++) {
        nodos[i] = nodos[i + 1];
    }
    (*n)--;

    return resultado;
}

/* Asigna un codigo a cada simbolo siguiendo las ramas del arbol. */
static void recorrer(const Nodo *nodo, uint64_t bits, int largo,
                     Codigo tabla[HUFF_SIMBOLOS])
{
    if (nodo->izq == NULL && nodo->der == NULL) {
        tabla[nodo->simbolo].bits  = bits;
        tabla[nodo->simbolo].largo = largo;
        return;
    }
    recorrer(nodo->izq, bits << 1,       largo + 1, tabla);
    recorrer(nodo->der, (bits << 1) | 1, largo + 1, tabla);
}

/* Funciones principales del arbol de Huffman. */

/*
 * Retorna NULL en dos casos: archivo vacio (todas las frecuencias en
 * cero) o falta de memoria. Quien llama distingue los dos casos
 * revisando si la tabla de frecuencias tiene algun valor distinto de 0.
 */
Nodo *construir_arbol(const uint64_t freq[HUFF_SIMBOLOS])
{
    Nodo *nodos[HUFF_SIMBOLOS];
    int n = 0;

    /* Crea una hoja para cada simbolo que aparece. */
    for (int s = 0; s < HUFF_SIMBOLOS; s++) {
        if (freq[s] == 0) {
            continue;
        }
        nodos[n] = crear_nodo(s, freq[s], NULL, NULL);
        if (nodos[n] == NULL) {
            for (int i = 0; i < n; i++) liberar_arbol(nodos[i]);
            return NULL;
        }
        n++;
    }

    if (n == 0) {
        return NULL;
    }

    /* Combina los nodos menos frecuentes hasta formar la raiz. */
    while (n > 1) {
        Nodo *izq = sacar_minimo(nodos, &n);
        Nodo *der = sacar_minimo(nodos, &n);

        Nodo *padre = crear_nodo(-1, izq->frecuencia + der->frecuencia, izq, der);
        if (padre == NULL) {
            liberar_arbol(izq);
            liberar_arbol(der);
            for (int i = 0; i < n; i++) liberar_arbol(nodos[i]);
            return NULL;
        }
        nodos[n] = padre;
        n++;
    }

    return nodos[0];
}

void generar_codigos(const Nodo *raiz, Codigo tabla[HUFF_SIMBOLOS])
{
    memset(tabla, 0, sizeof(Codigo) * HUFF_SIMBOLOS);

    if (raiz == NULL) {
        return;
    }

    /* Si solo aparece un simbolo, se le asigna el codigo 0. */
    if (raiz->izq == NULL && raiz->der == NULL) {
        tabla[raiz->simbolo].bits  = 0;
        tabla[raiz->simbolo].largo = 1;
        return;
    }

    recorrer(raiz, 0, 0, tabla);
}

void liberar_arbol(Nodo *raiz)
{
    if (raiz == NULL) {
        return;
    }
    liberar_arbol(raiz->izq);
    liberar_arbol(raiz->der);
    free(raiz);
}
