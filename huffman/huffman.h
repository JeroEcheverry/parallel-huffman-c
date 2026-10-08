/*
 * huffman.h -- interfaz publica del compresor Huffman.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Compresor Huffman concurrente: el archivo se divide en bloques que un
 * pool de hilos trabajadores procesa en paralelo (pthreads).
 *
 * Organizacion de los modulos:
 *   frecuencias.c  -> conteo de cuantas veces aparece cada byte
 *   arbol.c        -> construccion del arbol y tabla de codigos
 *   codificar.c    -> compresion concurrente (archivo -> .huff)
 *   decodificar.c  -> descompresion concurrente (.huff -> archivo)
 *   pool.c         -> pool de hilos que reparte tareas numeradas
 *   progreso.c     -> estado de avance compartido entre hilos
 *   io.c           -> lectura y escritura de archivos con syscalls
 *   main_huff.c    -> programa de prueba por linea de comandos
 */

#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stdint.h>   /* uint8_t, uint32_t, uint64_t */
#include <stddef.h>   /* size_t                      */
#include <sys/types.h> /* ssize_t                     */
#include <pthread.h>   /* pthread_mutex_t, pthread_cond_t */

/* Cantidad de simbolos posibles: un byte puede valer de 0 a 255. */
#define HUFF_SIMBOLOS 256

/* ------------------------------------------------------------------ */
/* Formato del archivo .huff (version 2, por bloques)                 */
/* ------------------------------------------------------------------ */
/*
 * El archivo original se divide en bloques de HUFF_TAM_BLOQUE bytes (el
 * ultimo puede ser mas corto). Todos los bloques se codifican con el
 * mismo arbol, construido con las frecuencias de TODO el archivo, pero
 * cada bloque comprimido empieza en un byte nuevo. Asi cada bloque se
 * puede comprimir y descomprimir sin depender de los demas.
 *
 *   Campo                  Tipo                  Bytes
 *   ---------------------  --------------------  ---------------
 *   firma "HUF2"           4 caracteres          4
 *   tamano original        uint64_t              8
 *   tamano de bloque       uint64_t              8
 *   numero de bloques      uint64_t              8
 *   tabla de frecuencias   uint64_t[256]         2048
 *   tabla de tamanos       uint64_t[n_bloques]   8 * n_bloques
 *   bloque 0, bloque 1...  bytes                 variable
 *
 * La tabla de tamanos guarda cuantos bytes comprimidos ocupa cada bloque.
 * Con ella se calcula donde empieza cada bloque sin decodificar los
 * anteriores, que es lo que permite descomprimir en paralelo.
 *
 * La tabla de frecuencias se guarda completa (tamano fijo) para
 * simplificar el codigo; el costo es despreciable en archivos grandes.
 */
#define HUFF_FIRMA        "HUF2"
#define HUFF_FIRMA_LARGO  4
#define HUFF_ENCABEZADO   (HUFF_FIRMA_LARGO + 3 * sizeof(uint64_t) \
                           + HUFF_SIMBOLOS * sizeof(uint64_t))

/* Bytes del archivo original que contiene cada bloque (64 KiB). */
#define HUFF_TAM_BLOQUE   (64 * 1024)

/* Largo maximo de un codigo: es lo que cabe en el campo 'bits' de Codigo. */
#define HUFF_MAX_LARGO    64

/* Limite de hilos trabajadores, para no agotar recursos del sistema. */
#define HUFF_MAX_HILOS    64

/* ------------------------------------------------------------------ */
/* Estructuras de datos                                               */
/* ------------------------------------------------------------------ */

/*
 * Nodo del arbol de Huffman.
 * En las hojas 'simbolo' es el byte (0..255).
 * En los nodos internos 'simbolo' vale -1 y solo importan los hijos.
 */
typedef struct Nodo {
    uint64_t     frecuencia;
    int          simbolo;
    struct Nodo *izq;
    struct Nodo *der;
} Nodo;

/*
 * Codigo de Huffman de un simbolo, guardado como bits en un entero
 * en lugar de un texto de '0' y '1'.
 *
 *   Ejemplo: el codigo "101" se guarda como bits = 0b101, largo = 3.
 *
 * largo = 0 significa que el simbolo no aparece en el archivo.
 */
typedef struct {
    uint64_t bits;
    int      largo;
} Codigo;

/*
 * Por que 64 bits: el codigo mas largo posible aparece cuando las
 * frecuencias crecen como la serie de Fibonacci. Un codigo de 33 bits ya
 * es posible en un archivo de unos 9 MB, asi que 32 bits no alcanzan.
 * Uno de 65 bits exigiria un archivo de mas de 10^13 bytes (10 TB), por
 * lo que 64 bits cubren cualquier archivo realista. Aun asi, codificar.c
 * verifica el limite y reporta error si se superara.
 */

/* ------------------------------------------------------------------ */
/* Frecuencias  (frecuencias.c)                                       */
/* ------------------------------------------------------------------ */

/*
 * Suma a 'freq' las apariciones de cada byte de 'buf'.
 * No pone 'freq' en cero: quien llama debe inicializarlo. Asi la misma
 * funcion sirve para acumular varios bloques (se usara en la fase 3).
 */
void contar_frecuencias(const unsigned char *buf, size_t n,
                        uint64_t freq[HUFF_SIMBOLOS]);

/* ------------------------------------------------------------------ */
/* Arbol y codigos  (arbol.c)                                         */
/* ------------------------------------------------------------------ */

/*
 * Construye el arbol a partir de la tabla de frecuencias.
 * Retorna NULL si todas las frecuencias son cero (archivo vacio).
 * El arbol debe salir identico siempre que las frecuencias sean
 * iguales, porque el descompresor lo reconstruye por su cuenta.
 */
Nodo *construir_arbol(const uint64_t freq[HUFF_SIMBOLOS]);

/* Llena 'tabla' con el codigo de cada simbolo recorriendo el arbol. */
void generar_codigos(const Nodo *raiz, Codigo tabla[HUFF_SIMBOLOS]);

/* Libera todos los nodos del arbol (recorrido en postorden). */
void liberar_arbol(Nodo *raiz);

/* ------------------------------------------------------------------ */
/* Entrada y salida  (io.c)                                           */
/* ------------------------------------------------------------------ */

/*
 * Lee un archivo completo a memoria. En *buf queda un bloque reservado
 * con malloc que quien llama debe liberar con free. Retorna 0 o -1.
 */
int huff_leer_archivo(const char *ruta, unsigned char **buf, size_t *n);

/*
 * Escribe los n bytes de buf en fd, repitiendo write las veces que haga
 * falta. Retorna n si todo se escribio, o -1 si hubo error.
 */
ssize_t huff_escribir_todo(int fd, const void *buf, size_t n);

/* ------------------------------------------------------------------ */
/* Progreso compartido  (progreso.c)                                  */
/* ------------------------------------------------------------------ */

/*
 * Estado de avance de una compresion o descompresion. Lo actualizan los
 * hilos trabajadores y lo consulta otro hilo (por ejemplo, el editor
 * para mostrar el porcentaje). Todos los campos se protegen con 'mutex'.
 *
 * 'cambio' es una variable de condicion que se senala cada vez que el
 * avance cambia. Quien quiera mostrar el progreso en vivo espera sobre
 * ella en lugar de preguntar en un ciclo (sin espera activa).
 *
 * Todas las funciones aceptan NULL y en ese caso no hacen nada, para
 * poder comprimir sin llevar la cuenta del progreso.
 */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cambio;
    uint64_t        hechos;      /* unidades de trabajo terminadas         */
    uint64_t        total;       /* unidades de trabajo de toda la tarea   */
    int             terminado;   /* 1 cuando la tarea acabo (bien o mal)   */
    int             cancelar;    /* 1 si alguien pidio detener la tarea    */
} HuffProgreso;

void huff_progreso_iniciar(HuffProgreso *p);
void huff_progreso_destruir(HuffProgreso *p);
void huff_progreso_fijar_total(HuffProgreso *p, uint64_t total);
void huff_progreso_avanzar(HuffProgreso *p, uint64_t n);
void huff_progreso_terminar(HuffProgreso *p);
void huff_progreso_cancelar(HuffProgreso *p);
int  huff_progreso_cancelado(HuffProgreso *p);

/* Copia el estado actual. Cualquier puntero de salida puede ser NULL. */
void huff_progreso_leer(HuffProgreso *p, uint64_t *hechos, uint64_t *total,
                        int *terminado);

/*
 * Bloquea al hilo que llama hasta que el progreso cambie o pasen
 * 'espera_ms' milisegundos. Retorna 1 si la tarea ya termino, 0 si no.
 */
int  huff_progreso_esperar(HuffProgreso *p, int espera_ms);

/* ------------------------------------------------------------------ */
/* Pool de hilos  (pool.c)                                            */
/* ------------------------------------------------------------------ */

/*
 * Una tarea procesa el elemento numero 'indice' (por ejemplo, un bloque).
 * Retorna 0 si salio bien o -1 si fallo; ante un fallo el pool deja de
 * repartir tareas nuevas.
 */
typedef int (*PoolTarea)(void *contexto, uint64_t indice);

/*
 * Reparto dinamico: los hilos comparten un contador 'siguiente'. Cada
 * hilo que queda libre toma el siguiente indice (protegido con 'mutex'),
 * de modo que un bloque lento no deja a los demas hilos sin trabajo.
 */
typedef struct {
    pthread_t      *hilos;
    int             n_hilos;      /* hilos creados de verdad              */
    pthread_mutex_t mutex;        /* protege 'siguiente' y 'error'        */
    uint64_t        siguiente;    /* proxima tarea sin asignar            */
    uint64_t        n_tareas;
    int             error;        /* 1 si alguna tarea fallo              */
    int             demora_ms;    /* pausa artificial por tarea (demos)   */
    PoolTarea       tarea;
    void           *contexto;
} Pool;

/* Numero de hilos a usar cuando no se indica: los nucleos disponibles. */
int huff_hilos_por_defecto(void);

/*
 * Crea los hilos y retorna de inmediato, mientras ellos trabajan.
 * Si n_hilos <= 0 se usa huff_hilos_por_defecto(). Retorna 0 o -1.
 */
int pool_iniciar(Pool *pool, int n_hilos, uint64_t n_tareas,
                 PoolTarea tarea, void *contexto);

/* Espera (join) a todos los hilos. Retorna 0, o -1 si alguna tarea fallo. */
int pool_esperar(Pool *pool);

/* pool_iniciar seguido de pool_esperar. */
int pool_ejecutar(int n_hilos, uint64_t n_tareas, PoolTarea tarea, void *contexto);

/* ------------------------------------------------------------------ */
/* Compresion y descompresion  (codificar.c, decodificar.c)           */
/* ------------------------------------------------------------------ */

/*
 * Ambas retornan 0 si todo salio bien y -1 si hubo error o se cancelo.
 *   n_hilos  : hilos trabajadores (0 = uno por nucleo).
 *   progreso : donde se reporta el avance (puede ser NULL). Al terminar
 *              siempre queda marcado como terminado.
 * Si fallan, borran el archivo de salida incompleto.
 */
int huff_comprimir(const char *ruta_entrada, const char *ruta_salida,
                   int n_hilos, HuffProgreso *progreso);
int huff_descomprimir(const char *ruta_entrada, const char *ruta_salida,
                      int n_hilos, HuffProgreso *progreso);

#endif /* HUFFMAN_H */
