/*
 * fondo.c -- compresion y descompresion en segundo plano dentro del editor.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 2
 *
 * Comandos nuevos:
 *   c [salida.huff]            comprime el archivo abierto
 *   k <entrada.huff> <salida>  descomprime (la salida puede ser el archivo abierto)
 *   e [v]                      muestra el progreso (v: en vivo, hasta que termine)
 *
 * Hilos que intervienen:
 *   - Hilo principal: el bucle del editor (repl.c). Nunca se bloquea
 *     esperando el archivo: si esta ocupado, el comando se rechaza.
 *   - Hilo de la tarea: uno por compresion/descompresion. Llama al modulo
 *     Huffman, que a su vez crea su propio pool de hilos trabajadores.
 *
 * Proteccion del archivo abierto (lectores-escritores con pthread_rwlock):
 *   - Comprimir LEE el archivo: la tarea toma el cerrojo de lectura durante
 *     toda la compresion. Los comandos que solo leen (p, s, m, y) pueden
 *     seguir usandose; los que modifican (a, d, i, x, u, r, o) se rechazan.
 *   - Descomprimir sobre el archivo abierto lo MODIFICA: primero se
 *     descomprime a un archivo temporal sin cerrojo, y solo para reemplazar
 *     el contenido se toma el cerrojo de escritura (unos milisegundos).
 *   - El hilo principal usa tryrdlock/trywrlock: si el cerrojo no esta
 *     disponible, retorna EBUSY de inmediato en vez de dormir, y la
 *     interfaz nunca se congela.
 */
#include "editor.h"

#include <errno.h>      /* EBUSY                         */
#include <signal.h>     /* sigaction, pthread_sigmask    */
#include <stdio.h>      /* printf, snprintf, perror      */
#include <string.h>     /* strncpy, strcmp, strerror     */
#include <sys/stat.h>   /* stat, fstat                   */
#include <unistd.h>     /* unlink                        */

/* ------------------------------------------------------------------ */
/* Senales                                                            */
/* ------------------------------------------------------------------ */

/*
 * El manejador de una senal solo puede hacer operaciones muy simples
 * (no printf, no malloc, no mutex). Por eso solo enciende esta bandera;
 * el bucle del editor la revisa y actua fuera del manejador.
 */
static volatile sig_atomic_t senal_recibida = 0;

static void manejador_sigint(int senal)
{
    (void)senal;
    senal_recibida = 1;
}

void fondo_instalar_senales(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejador_sigint;
    sigemptyset(&sa.sa_mask);
    /* Sin SA_RESTART: si llega Ctrl+C mientras fgets espera, la lectura se
       interrumpe y el bucle del editor puede atender la senal de una vez. */
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
}

int fondo_hubo_senal(void)
{
    if (senal_recibida) {
        senal_recibida = 0;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Utilidades                                                         */
/* ------------------------------------------------------------------ */

/*
 * Retorna 1 si 'ruta' es el mismo archivo que esta abierto en el editor.
 * Se comparan dispositivo e inodo (y no el texto de la ruta) porque
 * "notas.txt" y "./notas.txt" son el mismo archivo.
 */
static int es_el_archivo_abierto(const Editor *ed, const char *ruta)
{
    if (!ed_esta_abierto(ed)) return 0;

    struct stat a, b;
    if (stat(ruta, &a) != 0 || fstat(ed->fd, &b) != 0) return 0;

    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

/* Retorna 1 si dos rutas existentes son el mismo archivo. */
static int mismo_archivo(const char *r1, const char *r2)
{
    struct stat a, b;
    if (stat(r1, &a) != 0 || stat(r2, &b) != 0) return 0;
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

/* Porcentaje de avance de la tarea (0..100). */
static int porcentaje(TareaFondo *t)
{
    uint64_t hechos = 0, total = 0;
    int      terminado = 0;
    huff_progreso_leer(&t->progreso, &hechos, &total, &terminado);

    if (total == 0) return terminado ? 100 : 0;
    return (int)(hechos * 100 / total);
}

/* Retorna 1 si el hilo de la tarea ya termino su trabajo. */
static int tarea_terminada(TareaFondo *t)
{
    pthread_mutex_lock(&t->mutex);
    int terminada = t->terminada;
    pthread_mutex_unlock(&t->mutex);
    return terminada;
}

/* Duerme hasta que el hilo de la tarea termine (sin espera activa). */
static void esperar_fin_tarea(TareaFondo *t)
{
    pthread_mutex_lock(&t->mutex);
    while (!t->terminada) {
        pthread_cond_wait(&t->estado, &t->mutex);
    }
    pthread_mutex_unlock(&t->mutex);
}

/* Imprime una linea de estado con barra de progreso, terminada en 'fin'. */
static void imprimir_estado(TareaFondo *t, const char *fin)
{
    int  pct = porcentaje(t);
    char barra[31];
    int  llenos = pct * 30 / 100;

    for (int i = 0; i < 30; i++) {
        barra[i] = (i < llenos) ? '#' : '-';
    }
    barra[30] = '\0';

    printf("%s '%s' -> '%s' [%s] %3d%%%s",
           t->tipo == 'c' ? "Comprimiendo" : "Descomprimiendo",
           t->origen, t->destino, barra, pct, fin);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Hilo de la tarea                                                   */
/* ------------------------------------------------------------------ */

/* Avisa al hilo principal que la tarea ya arranco (y ya tiene su cerrojo). */
static void avisar_arranque(TareaFondo *t)
{
    pthread_mutex_lock(&t->mutex);
    t->arrancado = 1;
    pthread_cond_signal(&t->estado);
    pthread_mutex_unlock(&t->mutex);
}

/*
 * Descompresion en dos pasos:
 *   1. Sin cerrojo, hacia "<destino>.parcial": nadie mas usa ese archivo.
 *   2. Con cerrojo de escritura, se coloca el resultado: si el destino es
 *      el archivo abierto se reemplaza su contenido (queda en el historial
 *      y se puede deshacer); si no, se renombra el temporal al destino.
 * La comprobacion "es el archivo abierto?" se hace con el cerrojo tomado:
 * asi el usuario no puede abrir otro archivo entre la comprobacion y el
 * reemplazo.
 */
static int descomprimir_en_fondo(Editor *ed, TareaFondo *t)
{
    char temporal[ED_MAX_RUTA + 16];
    snprintf(temporal, sizeof(temporal), "%s.parcial", t->destino);

    if (huff_descomprimir(t->origen, temporal, 0, &t->progreso) < 0) {
        return -1;   /* huff_descomprimir ya borro el temporal incompleto */
    }

    int r;
    pthread_rwlock_wrlock(&ed->cerrojo);

    if (es_el_archivo_abierto(ed, t->destino)) {
        r = hist_reemplazar(ed, temporal);
        t->recargo_abierto = (r == 0);
        unlink(temporal);
    } else {
        r = rename(temporal, t->destino);
        if (r != 0) {
            perror("rename");
            unlink(temporal);
        }
    }

    pthread_rwlock_unlock(&ed->cerrojo);
    return r;
}

/* Funcion que ejecuta el hilo de la tarea. */
static void *hilo_tarea(void *arg)
{
    Editor     *ed = arg;
    TareaFondo *t  = &ed->tarea;
    int         r;

    if (t->tipo == 'c') {
        /* Lector: mientras se comprime nadie puede modificar el archivo. */
        pthread_rwlock_rdlock(&ed->cerrojo);
        avisar_arranque(t);
        r = huff_comprimir(t->origen, t->destino, 0, &t->progreso);
        pthread_rwlock_unlock(&ed->cerrojo);
    } else {
        avisar_arranque(t);
        r = descomprimir_en_fondo(ed, t);
    }

    pthread_mutex_lock(&t->mutex);
    t->resultado = r;
    t->terminada = 1;
    pthread_cond_broadcast(&t->estado);
    pthread_mutex_unlock(&t->mutex);
    return NULL;
}

/*
 * Crea el hilo de la tarea. SIGINT se bloquea mientras se crea, porque el
 * hilo nuevo hereda la mascara de senales: asi Ctrl+C siempre lo recibe el
 * hilo principal, y tambien los trabajadores del pool, que el hilo de la
 * tarea crea despues, quedan con SIGINT bloqueado.
 */
static int lanzar_tarea(Editor *ed, char tipo, const char *origen, const char *destino)
{
    TareaFondo *t = &ed->tarea;

    t->tipo = tipo;
    strncpy(t->origen,  origen,  ED_MAX_RUTA - 1);
    strncpy(t->destino, destino, ED_MAX_RUTA - 1);
    t->origen[ED_MAX_RUTA - 1]  = '\0';
    t->destino[ED_MAX_RUTA - 1] = '\0';
    t->arrancado       = 0;
    t->terminada       = 0;
    t->resultado       = 0;
    t->recargo_abierto = 0;

    /* Ningun otro hilo usa el progreso ahora: se reinicia desde cero. */
    huff_progreso_destruir(&t->progreso);
    huff_progreso_iniciar(&t->progreso);

    sigset_t bloquear, anterior;
    sigemptyset(&bloquear);
    sigaddset(&bloquear, SIGINT);
    pthread_sigmask(SIG_BLOCK, &bloquear, &anterior);

    int r = pthread_create(&t->hilo, NULL, hilo_tarea, ed);

    pthread_sigmask(SIG_SETMASK, &anterior, NULL);

    if (r != 0) {
        fprintf(stderr, "Error: no se pudo crear el hilo (%s)\n", strerror(r));
        return -1;
    }
    t->activa = 1;

    /* Esperar (dormido, sin espera activa) a que la tarea tome su cerrojo.
       Asi, cuando el usuario escribe el siguiente comando, el archivo ya
       esta protegido. */
    pthread_mutex_lock(&t->mutex);
    while (!t->arrancado) {
        pthread_cond_wait(&t->estado, &t->mutex);
    }
    pthread_mutex_unlock(&t->mutex);

    printf("%s en segundo plano: '%s' -> '%s'. Puedes seguir usando el editor"
           " ('e' muestra el progreso).\n",
           tipo == 'c' ? "Compresion" : "Descompresion", origen, destino);
    return 0;
}

/* Imprime el resultado de una tarea ya terminada (despues del join). */
static void informar_resultado(Editor *ed)
{
    TareaFondo *t = &ed->tarea;
    int cancelada = huff_progreso_cancelado(&t->progreso);

    if (t->resultado != 0) {
        printf("[segundo plano] %s '%s' %s.\n",
               t->tipo == 'c' ? "La compresion de" : "La descompresion de",
               t->origen, cancelada ? "fue cancelada" : "fallo");
        return;
    }

    if (t->tipo == 'c') {
        struct stat a, b;
        if (stat(t->origen, &a) == 0 && stat(t->destino, &b) == 0 && a.st_size > 0) {
            printf("[segundo plano] Compresion terminada: '%s' (%lld bytes) -> '%s'"
                   " (%lld bytes, %.1f%% del original).\n",
                   t->origen, (long long)a.st_size, t->destino, (long long)b.st_size,
                   100.0 * (double)b.st_size / (double)a.st_size);
        } else {
            printf("[segundo plano] Compresion terminada: '%s' -> '%s'.\n",
                   t->origen, t->destino);
        }
    } else {
        printf("[segundo plano] Descompresion terminada: '%s' -> '%s'.\n",
               t->origen, t->destino);
        if (t->recargo_abierto) {
            printf("El archivo abierto se actualizo (%zu lineas). Usa 'u' para deshacer.\n",
                   ed->n_lineas);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Ciclo de vida                                                      */
/* ------------------------------------------------------------------ */

void fondo_iniciar(Editor *ed)
{
    pthread_rwlock_init(&ed->cerrojo, NULL);

    TareaFondo *t = &ed->tarea;
    t->activa     = 0;
    t->arrancado  = 0;
    t->terminada  = 0;
    t->resultado  = 0;
    t->tipo       = 0;
    t->origen[0]  = '\0';
    t->destino[0] = '\0';
    t->recargo_abierto = 0;
    pthread_mutex_init(&t->mutex, NULL);
    pthread_cond_init(&t->estado, NULL);
    huff_progreso_iniciar(&t->progreso);
}

void fondo_destruir(Editor *ed)
{
    TareaFondo *t = &ed->tarea;
    huff_progreso_destruir(&t->progreso);
    pthread_cond_destroy(&t->estado);
    pthread_mutex_destroy(&t->mutex);
    pthread_rwlock_destroy(&ed->cerrojo);
}

/* Si la tarea ya termino, le hace join e informa. Se llama antes de cada prompt. */
void fondo_revisar(Editor *ed)
{
    TareaFondo *t = &ed->tarea;
    if (!t->activa) return;

    if (!tarea_terminada(t)) return;

    pthread_join(t->hilo, NULL);   /* el hilo ya termino: no bloquea */
    t->activa = 0;
    informar_resultado(ed);
}

/* Al salir del editor: si hay una tarea, se cancela y se espera (join limpio). */
void fondo_finalizar(Editor *ed)
{
    TareaFondo *t = &ed->tarea;
    if (!t->activa) return;

    /* Si el trabajo de Huffman ya acabo (solo falta colocar el resultado),
       no se cancela: se espera unos milisegundos a que termine. */
    int huff_terminado = 0;
    huff_progreso_leer(&t->progreso, NULL, NULL, &huff_terminado);

    if (!tarea_terminada(t) && !huff_terminado) {
        printf("Cancelando la tarea en segundo plano antes de salir...\n");
        huff_progreso_cancelar(&t->progreso);
    }
    pthread_join(t->hilo, NULL);
    t->activa = 0;
    informar_resultado(ed);
}

void fondo_atender_senal(Editor *ed)
{
    TareaFondo *t = &ed->tarea;

    if (t->activa && !huff_progreso_cancelado(&t->progreso)) {
        printf("Ctrl+C: cancelando la tarea en segundo plano...\n");
        huff_progreso_cancelar(&t->progreso);
    } else {
        printf("Ctrl+C: no hay tarea que cancelar. Usa 'q' para salir.\n");
    }
}

void fondo_texto_prompt(Editor *ed, char *buf, size_t n)
{
    TareaFondo *t = &ed->tarea;
    if (t->activa) {
        snprintf(buf, n, "ed [%c %d%%]> ", t->tipo, porcentaje(t));
    } else {
        snprintf(buf, n, "ed> ");
    }
}

/* ------------------------------------------------------------------ */
/* Cerrojo de lectores-escritores para el hilo principal              */
/* ------------------------------------------------------------------ */

int fondo_tomar_cerrojo(Editor *ed, Acceso acceso)
{
    int r = 0;
    if (acceso == ACCESO_LECTOR)   r = pthread_rwlock_tryrdlock(&ed->cerrojo);
    if (acceso == ACCESO_ESCRITOR) r = pthread_rwlock_trywrlock(&ed->cerrojo);
    return (r == 0) ? 0 : -1;      /* r == EBUSY: otro hilo lo tiene */
}

void fondo_soltar_cerrojo(Editor *ed, Acceso acceso)
{
    if (acceso != ACCESO_LIBRE) {
        pthread_rwlock_unlock(&ed->cerrojo);
    }
}

/* ------------------------------------------------------------------ */
/* Comandos                                                           */
/* ------------------------------------------------------------------ */

/* c [salida.huff]  --  comprimir el archivo abierto en segundo plano. */
int cmd_c(Editor *ed, const char *arg)
{
    if (!ed_esta_abierto(ed)) {
        printf("Error: no hay ningun archivo abierto. Usa: o <archivo>\n");
        return -1;
    }
    if (ed->tarea.activa) {
        printf("Ya hay una tarea en segundo plano. Usa 'e' para ver su progreso.\n");
        return -1;
    }

    char destino[ED_MAX_RUTA];
    if (arg == NULL || *arg == '\0') {
        if (strlen(ed->ruta) + 5 >= sizeof(destino)) {
            printf("Error: la ruta es demasiado larga.\n");
            return -1;
        }
        snprintf(destino, sizeof(destino), "%s.huff", ed->ruta);
    } else {
        strncpy(destino, arg, sizeof(destino) - 1);
        destino[sizeof(destino) - 1] = '\0';
    }

    if (es_el_archivo_abierto(ed, destino)) {
        printf("Error: la salida no puede ser el mismo archivo que se esta comprimiendo.\n");
        return -1;
    }

    return lanzar_tarea(ed, 'c', ed->ruta, destino);
}

/* k <entrada.huff> <salida>  --  descomprimir en segundo plano. */
int cmd_k(Editor *ed, const char *arg)
{
    if (ed->tarea.activa) {
        printf("Ya hay una tarea en segundo plano. Usa 'e' para ver su progreso.\n");
        return -1;
    }

    /* %511s: lee una palabra de hasta 511 caracteres (ED_MAX_RUTA - 1). */
    char origen[ED_MAX_RUTA], destino[ED_MAX_RUTA];
    if (arg == NULL || sscanf(arg, "%511s %511s", origen, destino) != 2) {
        printf("Uso: k <entrada.huff> <salida>\n");
        return -1;
    }
    if (strcmp(origen, destino) == 0 || mismo_archivo(origen, destino)) {
        printf("Error: la entrada y la salida deben ser archivos distintos.\n");
        return -1;
    }

    return lanzar_tarea(ed, 'k', origen, destino);
}

/* e [v]  --  estado de la tarea; con 'v' se muestra en vivo hasta que termine. */
int cmd_e(Editor *ed, const char *arg)
{
    TareaFondo *t = &ed->tarea;

    if (!t->activa) {
        printf("No hay ninguna tarea en segundo plano.\n");
        return 0;
    }

    if (arg == NULL || arg[0] != 'v') {
        imprimir_estado(t, "\n");
        return 0;
    }

    printf("Progreso en vivo (Ctrl+C vuelve al editor sin cancelar la tarea):\n");

    /*
     * huff_progreso_esperar duerme al hilo hasta que el progreso cambie o
     * pasen 200 ms; no consume CPU mientras espera (sin espera activa).
     */
    while (!tarea_terminada(t)) {
        imprimir_estado(t, "\r");
        int huff_terminado = huff_progreso_esperar(&t->progreso, 200);
        if (fondo_hubo_senal()) break;
        if (huff_terminado) {
            /* Huffman acabo; solo falta que la tarea coloque el resultado. */
            esperar_fin_tarea(t);
        }
    }
    imprimir_estado(t, "\n");
    return 0;
}
