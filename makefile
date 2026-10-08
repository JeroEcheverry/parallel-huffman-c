# ====================================================================================
#  Makefile  --  Compresor Huffman concurrente + editor (SO2026B - EAFIT) - Parcial 2
# ====================================================================================
#  make             compila ./huff (compresor de prueba) y ./editor_huff (editor integrado)
#  make test        prueba de integridad (md5) con 1, 2, 4 y 8 hilos
#  make test-editor prueba del editor: tarea en segundo plano y archivo ocupado
#  make tsan        compila con ThreadSanitizer y corre las pruebas buscando
#                   condiciones de carrera
#  make benchmark   mide tiempos con distinta cantidad de hilos
#  make clean       borra binarios, objetos y archivos de prueba
# ====================================================================================

CC      = gcc

# -pthread      : enlaza la biblioteca de hilos POSIX.
# -D_GNU_SOURCE : expone las declaraciones POSIX/GNU (ftruncate, etc.).
CFLAGS  = -Wall -Wextra -std=gnu99 -g -O2 -pthread -D_GNU_SOURCE -Ihuffman -Ieditor

# Modulo Huffman (lo usan los dos programas).
HUFF_SRCS = huffman/frecuencias.c huffman/arbol.c huffman/codificar.c \
            huffman/decodificar.c huffman/io.c huffman/pool.c huffman/progreso.c
HUFF_OBJS = $(HUFF_SRCS:.c=.o)

# Editor del parcial 1 + modulo de tareas en segundo plano.
ED_SRCS   = editor/main.c editor/repl.c editor/archivo.c editor/edicion.c \
            editor/busqueda.c editor/portapapeles.c editor/historial.c \
            editor/comandos.c editor/fondo.c
ED_OBJS   = $(ED_SRCS:.c=.o)

all: huff editor_huff

huff: huffman/main_huff.o $(HUFF_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

editor_huff: $(ED_OBJS) $(HUFF_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Si cambia una cabecera se recompila todo lo que depende de ella.
huffman/%.o: huffman/%.c huffman/huffman.h
	$(CC) $(CFLAGS) -c $< -o $@

editor/%.o: editor/%.c editor/editor.h huffman/huffman.h
	$(CC) $(CFLAGS) -c $< -o $@

test: huff
	bash pruebas/verificar.sh

test-editor: editor_huff
	bash pruebas/prueba_editor.sh

# ThreadSanitizer instrumenta cada acceso a memoria y reporta cualquier
# par de accesos de hilos distintos sin sincronizacion (condicion de carrera).
TSAN_FLAGS = -Wall -Wextra -std=gnu99 -g -O1 -pthread -D_GNU_SOURCE \
             -Ihuffman -Ieditor -fsanitize=thread

tsan:
	$(CC) $(TSAN_FLAGS) -o huff_tsan huffman/main_huff.c $(HUFF_SRCS)
	$(CC) $(TSAN_FLAGS) -o editor_huff_tsan $(ED_SRCS) $(HUFF_SRCS)
	HUFF=./huff_tsan bash pruebas/verificar.sh
	EDITOR_BIN=./editor_huff_tsan bash pruebas/prueba_editor.sh

benchmark: huff
	bash pruebas/benchmark.sh

clean:
	rm -f huff editor_huff huff_tsan editor_huff_tsan huffman/*.o editor/*.o
	rm -rf pruebas/datos pruebas/salida

.PHONY: all test test-editor tsan benchmark clean
