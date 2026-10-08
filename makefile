# Compila el compresor y el editor, y define sus pruebas.

CC      = gcc

# Opciones comunes de compilacion.
CFLAGS  = -Wall -Wextra -std=gnu99 -g -O2 -pthread -D_GNU_SOURCE -Ihuffman -Ieditor

# Archivos que forman cada programa.
HUFF_SRCS = huffman/frecuencias.c huffman/arbol.c huffman/codificar.c \
            huffman/decodificar.c huffman/io.c huffman/pool.c huffman/progreso.c
HUFF_OBJS = $(HUFF_SRCS:.c=.o)

ED_SRCS   = editor/main.c editor/repl.c editor/archivo.c editor/edicion.c \
            editor/busqueda.c editor/portapapeles.c editor/historial.c \
            editor/comandos.c editor/fondo.c
ED_OBJS   = $(ED_SRCS:.c=.o)

all: huff editor_huff

huff: huffman/main_huff.o $(HUFF_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

editor_huff: $(ED_OBJS) $(HUFF_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Reglas para compilar los archivos fuente.
huffman/%.o: huffman/%.c huffman/huffman.h
	$(CC) $(CFLAGS) -c $< -o $@

editor/%.o: editor/%.c editor/editor.h huffman/huffman.h
	$(CC) $(CFLAGS) -c $< -o $@

test: huff
	bash pruebas/verificar.sh

test-editor: editor_huff
	bash pruebas/prueba_editor.sh

# Pruebas con ThreadSanitizer para detectar problemas entre hilos.
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
