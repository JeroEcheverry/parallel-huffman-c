/* Lee los comandos del usuario y los ejecuta mientras el editor esta abierto. */
#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define MAX_ENTRADA 8192

/* Cada comando tiene su descripcion, acceso al archivo y funcion. */

typedef int (*Manejador)(Editor *ed, const char *arg);

typedef struct {
    char        clave;
    const char *uso;
    const char *descripcion;
    Acceso      acceso;
    Manejador   fn;
} ComandoEd;

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

/* Prepara el argumento que se pasa al comando. */
static char *separar_argumento(char *linea)
{
    char *p = linea + 1;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static void quitar_salto(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

/* Bucle principal: recibe comandos y cierra los recursos al salir. */
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
        fondo_revisar(&ed);
        if (fondo_hubo_senal()) {
            fondo_atender_senal(&ed);
        }

        fondo_texto_prompt(&ed, prompt, sizeof(prompt));
        printf("%s", prompt);
        fflush(stdout);

        if (fgets(entrada, sizeof(entrada), stdin) == NULL) {
            if (ferror(stdin) && errno == EINTR) {
                clearerr(stdin);
                printf("\n");
                continue;
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

    fondo_finalizar(&ed);
    ed_cerrar(&ed);
    fondo_destruir(&ed);
    printf("Editor cerrado. Hasta luego.\n");

    return 0;
}
