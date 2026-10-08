/*
 * arbol.c -- construccion del arbol de Huffman y tabla de codigos.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Misma logica de huffman_tree.cpp (version C++ del curso de Estructuras
 * de Datos): se crea una hoja por simbolo y se combinan repetidamente los
 * dos nodos de menor frecuencia hasta que queda uno solo, la raiz.
 */
#include "huffman.h"

#include <stdlib.h>   /* malloc, free */
#include <string.h>   /* memset       */

/* ------------------------------------------------------------------ */
/* Funciones auxiliares (solo se usan dentro de este archivo)         */
/* ------------------------------------------------------------------ */

/* Reserva e inicializa un nodo. Retorna NULL si no hay memoria. */
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

/*
 * Saca del arreglo el nodo de menor frecuencia y lo retorna.
 * Equivale a findMinIndex + erase de la version C++.
 *
 * Si hay empate gana el primero que aparece en el arreglo. Como el
 * arreglo siempre se llena en el mismo orden (simbolos de 0 a 255),
 * el compresor y el descompresor toman las mismas decisiones y
 * construyen exactamente el mismo arbol.
 */
static Nodo *sacar_minimo(Nodo *nodos[], int *n)
{
    int min = 0;
    for (int i = 1; i < *n; i++) {
        if (nodos[i]->frecuencia < nodos[min]->frecuencia) {
            min = i;
        }
    }

    Nodo *resultado = nodos[min];

    /* Se corren una posicion a la izquierda los nodos que estaban despues. */
    for (int i = min; i < *n - 1; i++) {
        nodos[i] = nodos[i + 1];
    }
    (*n)--;

    return resultado;
}

/*
 * Recorre el arbol en preorden armando el codigo de cada hoja.
 * Bajar a la izquierda agrega un 0 al final del codigo y bajar a la
 * derecha agrega un 1:
 *
 *   bits << 1        -> corre los bits y deja un 0 al final
 *   (bits << 1) | 1  -> corre los bits y deja un 1 al final
 */
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

/* ------------------------------------------------------------------ */
/* Funciones publicas                                                 */
/* ------------------------------------------------------------------ */

/*
 * Retorna NULL en dos casos: archivo vacio (todas las frecuencias en
 * cero) o falta de memoria. Quien llama distingue los dos casos
 * revisando si la tabla de frecuencias tiene algun valor distinto de 0.
 */
Nodo *construir_arbol(const uint64_t freq[HUFF_SIMBOLOS])
{
    Nodo *nodos[HUFF_SIMBOLOS];
    int n = 0;

    /* Paso 1: una hoja por cada simbolo que aparece en el archivo. */
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

    /* Paso 2: combinar los dos de menor frecuencia hasta que quede uno. */
    while (n > 1) {
        Nodo *izq = sacar_minimo(nodos, &n);
        Nodo *der = sacar_minimo(nodos, &n);

        /* Los nodos internos no representan un simbolo: se marcan con -1. */
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
    /* largo = 0 en todos: ningun simbolo tiene codigo hasta que se asigne. */
    memset(tabla, 0, sizeof(Codigo) * HUFF_SIMBOLOS);

    if (raiz == NULL) {
        return;
    }

    /*
     * Caso especial: el archivo tiene un solo simbolo distinto (ej. "aaaa").
     * El arbol es una sola hoja y el recorrido le daria un codigo de largo 0,
     * que no se puede escribir. Se le asigna el codigo "0", igual que en la
     * version C++.
     */
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
