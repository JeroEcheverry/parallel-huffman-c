/*
 * repl.c -- bucle interactivo y despacho de comandos del editor.
 * Universidad EAFIT - Sistemas Operativos (SO2026B) - Parcial 1
 *
 * Ciclo lectura-evaluacion-impresion: lee una linea con fgets, toma el
 * primer caracter como comando y el resto como argumento, ejecuta el
 * manejador correspondiente y repite hasta 'q' o Ctrl+D.
 *
 * El bucle esta en editor_ejecutar (no en main) para poder llamarlo tanto
 * desde main.c (programa independiente) como desde cat_edicion.c (shell
 * eafitOS).
 *
 * Parcial 2: cada comando declara si lee o modifica el archivo abierto.
 * Antes de ejecutarlo se intenta tomar el cerrojo de lectores-escritores
 * sin bloquear; si una tarea en segundo plano lo tiene, el comando se
 * rechaza con un mensaje y el editor sigue respondiendo.
 */
#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define MAX_ENTRADA 8192

/* ---------------------------------------------------------------- */
/* Tabla de comandos                                                   */
/* ---------------------------------------------------------------- */
/* Mismo patron del shell de la asignatura: tabla de punteros a funcion
   en vez de una cadena de if/else. Agregar un comando es escribir su
   manejador y anadir una fila aqui. */

typedef int (*Manejador)(Editor *ed, const char *arg);

typedef struct {
    char        clave;
    const char *uso;
    const char *descripcion;
    Acceso      acceso;     /* que cerrojo necesita sobre el archivo abierto */
    Manejador   fn;
} ComandoEd;

/* 'o' es escritor aunque no cambie el contenido: cambia cual es el archivo
   abierto (ed->fd, ed->ruta), y eso tambien lo usa la tarea en segundo plano. */
static const ComandoEd tabla[] = {
    { 'o', "o <archivo>",  "Abre un archivo (lo crea si no existe).",   ACCESO_ESCRITOR, cmd_o },
    { 'p', "p [n]",        "Imprime la linea n, o todo el archivo.",    ACCESO_LECTOR,   cmd_p },
    { 'a', "a <texto>",    "Anade el texto como linea al final.",       ACCESO_ESCRITOR, cmd_a },
    { 'd', "d <n>",        "Borra la linea n.",                         ACCESO_ESCRITOR, cmd_d },
    { 'i', "i <n> <txt>",  "Inserta el texto como nueva linea n.",      ACCESO_ESCRITOR, cmd_i },
    { 's', "s <palabra>",  "Busca una palabra e imprime las lineas.",   ACCESO_LECTOR,   cmd_s },
    { 'm', "m",            "Muestra los metadatos del archivo.",        ACCESO_LECTOR,   cmd_m },
    { 'y', "y <n>",        "Copia la linea n al portapapeles.",         ACCESO_LECTOR,   cmd_y },
    { 'x', "x <n>",        "Pega el portapapeles como nueva linea n.",  ACCESO_ESCRITOR, cmd_x },
    { 'u', "u",            "Deshace la ultima modificacion.",           ACCESO_ESCRITOR, cmd_u },
    { 'r', "r",            "Rehace la modificacion deshecha.",          ACCESO_ESCRITOR, cmd_r },
    { 'c', "c [salida]",   "Comprime el archivo en segundo plano.",     ACCESO_LIBRE,    cmd_c },
    { 'k', "k <ent> <sal>","Descomprime un .huff en segundo plano.",    ACCESO_LIBRE,    cmd_k },
    { 'e', "e [v]",        "Progreso de la tarea (v: en vivo).",        ACCESO_LIBRE,    cmd_e },
};

static const int n_comandos = (int)(sizeof(tabla) / sizeof(tabla[0]));

static void mostrar_ayuda(void)
{
    printf("\nComandos disponibles:\n");
    for (int i = 0; i < n_comandos; i++) {
        printf("  %-14s %s\n", tabla[i].uso, tabla[i].descripcion);
    }
    printf("  %-14s %s\n", "h", "Muestra esta ayuda.");
    printf("  %-14s %s\n", "q", "Cierra el archivo y sale.");
    printf("\n");
}

/*
 * Separa el comando (primer caracter) de su argumento. A diferencia del
 * shell de la asignatura, aqui no se tokeniza: todo lo que sigue al
 * comando es el argumento tal cual, para que 'a hola mundo' conserve los
 * espacios (mismo comportamiento que el editor ed de Unix).
 *
 * Nunca devuelve NULL: si no hay argumento, apunta al terminador nulo.
 */
static char *separar_argumento(char *linea)
{
    char *p = linea + 1;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Quita el salto de linea que deja fgets al final de la cadena. */
static void quitar_salto(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

/*
 * Bucle principal del editor. 'ruta_inicial' es el archivo a abrir al
 * arrancar, o NULL para empezar sin archivo. Libera todos los recursos
 * antes de retornar (incluida la tarea en segundo plano). Siempre retorna 0.
 */
int editor_ejecutar(const char *ruta_inicial)
{
    Editor ed;
    ed_init(&ed);
    fondo_iniciar(&ed);
    fondo_instalar_senales();

    char entrada[MAX_ENTRADA];
    char prompt[64];

    if (ruta_inicial != NULL) {
        cmd_o(&ed, ruta_inicial);
    }

    while (1) {
        /* Antes de cada prompt: informar si la tarea termino y atender Ctrl+C. */
        fondo_revisar(&ed);
        if (fondo_hubo_senal()) {
            fondo_atender_senal(&ed);
        }

        fondo_texto_prompt(&ed, prompt, sizeof(prompt));
        printf("%s", prompt);
        fflush(stdout);   /* el prompt no lleva '\n', hay que forzar el vaciado */

        if (fgets(entrada, sizeof(entrada), stdin) == NULL) {
            /* Ctrl+C interrumpe la lectura (errno == EINTR): no es fin de archivo. */
            if (ferror(stdin) && errno == EINTR) {
                clearerr(stdin);
                printf("\n");
                continue;   /* la senal se atiende al inicio del ciclo */
            }
            printf("\n");
            break;
        }

        quitar_salto(entrada);

        if (entrada[0] == '\0') continue;

        char clave = entrada[0];

        if (clave == 'q') {
            break;
        }
        if (clave == 'h') {
            mostrar_ayuda();
            continue;
        }

        char *arg = separar_argumento(entrada);

        int encontrado = 0;
        for (int i = 0; i < n_comandos; i++) {
            if (tabla[i].clave == clave) {
                encontrado = 1;

                /* Lectores-escritores: el cerrojo se intenta sin bloquear. */
                if (fondo_tomar_cerrojo(&ed, tabla[i].acceso) == -1) {
                    printf("Archivo ocupado por la tarea en segundo plano: '%c' %s."
                           " Intenta cuando termine ('e' muestra el progreso).\n",
                           clave, tabla[i].acceso == ACCESO_ESCRITOR
                                  ? "modifica el archivo" : "lee el archivo");
                    break;
                }
                tabla[i].fn(&ed, arg);
                fondo_soltar_cerrojo(&ed, tabla[i].acceso);
                break;
            }
        }

        if (!encontrado) {
            printf("Comando '%c' no reconocido. Escribe 'h' para la ayuda.\n", clave);
        }
    }

    /* Primero se cancela y espera la tarea (join); despues se cierra el archivo. */
    fondo_finalizar(&ed);
    ed_cerrar(&ed);
    fondo_destruir(&ed);
    printf("Editor cerrado. Hasta luego.\n");

    return 0;
}
