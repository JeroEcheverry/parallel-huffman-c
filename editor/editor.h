/* Estructuras y funciones que comparten los modulos del editor. */

#ifndef EDITOR_H
#define EDITOR_H

#include <sys/types.h>
#include <stddef.h>
#include <pthread.h>

#include "huffman.h"

/* Valores usados por los buffers y el historial. */

#define ED_MAX_RUTA      512
#define ED_BLOQUE       4096
#define ED_CAP_INICIAL    64
#define ED_MAX_VERSIONES  50

/* Datos principales que usa el editor. */

typedef struct {
    off_t  offset;
    size_t largo;
} Linea;

typedef struct {
    char rutas[ED_MAX_VERSIONES][ED_MAX_RUTA];
    int  n;
    int  actual;
    int  contador;
} Historial;

/* Estado de una tarea de compresion o descompresion en segundo plano. */
typedef struct {
    pthread_t       hilo;
    int             activa;
    pthread_mutex_t mutex;
    pthread_cond_t  estado;
    int             arrancado;
    int             terminada;
    int             resultado;
    char            tipo;
    char            origen[ED_MAX_RUTA];
    char            destino[ED_MAX_RUTA];
    int             recargo_abierto;
    HuffProgreso    progreso;
} TareaFondo;

/* Tipo de acceso que necesita cada comando al archivo. */
typedef enum {
    ACCESO_LIBRE,
    ACCESO_LECTOR,
    ACCESO_ESCRITOR
} Acceso;

/* Estado general del editor. */
typedef struct {
    int    fd;
    char   ruta[ED_MAX_RUTA];

    Linea *lineas;
    size_t n_lineas;
    size_t cap;

    off_t  tam;

    char  *portapapeles;
    size_t portapapeles_largo;

    Historial hist;

    pthread_rwlock_t cerrojo;
    TareaFondo       tarea;
} Editor;

/* Funciones para abrir, leer y modificar el archivo. */

void ed_init(Editor *ed);
int  ed_esta_abierto(const Editor *ed);
int  ed_abrir(Editor *ed, const char *ruta);
int  ed_cerrar(Editor *ed);
int  ed_indexar(Editor *ed);
int  ed_imprimir_linea(Editor *ed, size_t idx);

int  ed_anexar(Editor *ed, const char *texto);
int  ed_insertar(Editor *ed, size_t idx, const char *texto);
int  ed_borrar_linea(Editor *ed, size_t idx);
char *ed_linea_a_memoria(Editor *ed, size_t idx, size_t *largo_out);

ssize_t leer_exacto(int fd, void *buf, size_t n);
ssize_t escribir_todo(int fd, const void *buf, size_t n);

/* Inicio del editor interactivo. */
int editor_ejecutar(const char *ruta_inicial);

/* Busqueda y datos del archivo. */
int ed_buscar(Editor *ed, const char *palabra);
int ed_metadatos(Editor *ed);

int  ed_copiar(Editor *ed, size_t idx);
int  ed_pegar(Editor *ed, size_t idx);
void ed_portapapeles_liberar(Editor *ed);

/* Historial de cambios. */
void hist_init(Editor *ed);
int  hist_registrar(Editor *ed);
int  hist_deshacer(Editor *ed);
int  hist_rehacer(Editor *ed);
void hist_limpiar(Editor *ed);
int  hist_reemplazar(Editor *ed, const char *ruta);

/* Comandos disponibles en la interfaz. */
int cmd_o(Editor *ed, const char *arg);
int cmd_p(Editor *ed, const char *arg);
int cmd_a(Editor *ed, const char *arg);
int cmd_d(Editor *ed, const char *arg);
int cmd_i(Editor *ed, const char *arg);
int cmd_s(Editor *ed, const char *arg);
int cmd_m(Editor *ed, const char *arg);
int cmd_y(Editor *ed, const char *arg);
int cmd_x(Editor *ed, const char *arg);
int cmd_u(Editor *ed, const char *arg);
int cmd_r(Editor *ed, const char *arg);

/* Compresion y descompresion en segundo plano. */

int  cmd_c(Editor *ed, const char *arg);
int  cmd_k(Editor *ed, const char *arg);
int  cmd_e(Editor *ed, const char *arg);

void fondo_iniciar(Editor *ed);
void fondo_destruir(Editor *ed);
void fondo_revisar(Editor *ed);
void fondo_finalizar(Editor *ed);
void fondo_texto_prompt(Editor *ed, char *buf, size_t n);

/* Cerrojos para coordinar comandos y tareas en segundo plano. */
int  fondo_tomar_cerrojo(Editor *ed, Acceso acceso);
void fondo_soltar_cerrojo(Editor *ed, Acceso acceso);

/* Manejo de Ctrl+C. */
void fondo_instalar_senales(void);
int  fondo_hubo_senal(void);
void fondo_atender_senal(Editor *ed);

#endif
