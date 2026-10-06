# ====================================================================================
#  Makefile  --  Compresor Huffman concurrente (SO2026B - EAFIT) - Parcial 2
# ====================================================================================
#  make          compila el programa de prueba ./huff
#  make test     comprime y descomprime los archivos de pruebas/datos y compara
#  make clean    borra binarios y objetos
# ====================================================================================

CC      = gcc

# -pthread      : necesario desde la fase 3 (hilos); se deja desde ya para no olvidarlo.
# -D_GNU_SOURCE : expone las declaraciones POSIX/GNU, igual que en el editor.
CFLAGS  = -Wall -Wextra -std=gnu99 -g -pthread -D_GNU_SOURCE -Ihuffman

TARGET  = huff
SRCS    = huffman/main_huff.c    \
          huffman/frecuencias.c  \
          huffman/arbol.c        \
          huffman/codificar.c    \
          huffman/decodificar.c  \
          huffman/io.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

# Cada .c depende tambien de la cabecera: si cambia huffman.h se recompila todo.
huffman/%.o: huffman/%.c huffman/huffman.h
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	bash pruebas/verificar.sh

clean:
	rm -f $(TARGET) $(OBJS)
	rm -rf pruebas/salida

.PHONY: all test clean