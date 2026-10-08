#!/bin/bash
# ====================================================================================
#  verificar.sh  --  Prueba de integridad del compresor Huffman concurrente
# ====================================================================================
#  Por cada archivo de pruebas/datos y por cada cantidad de hilos (1, 2, 4 y 8):
#    1. Lo comprime y lo descomprime.
#    2. Compara el md5 del original con el del recuperado.
#  Ademas comprueba que el .huff sea identico sin importar cuantos hilos se usen:
#  si el escritor no respetara el orden de los bloques, los archivos diferirian.
#
#  Variable opcional: HUFF=<binario> para probar otra compilacion (ej. ./huff_tsan).
# ====================================================================================

DIR=$(dirname "$0")
DATOS="$DIR/datos"
SALIDA="$DIR/salida"
HUFF="${HUFF:-$DIR/../huff}"
HILOS="1 2 4 8"

mkdir -p "$DATOS" "$SALIDA"

# --- Casos de prueba ---------------------------------------------------------------
: > "$DATOS/vacio.txt"                                   # archivo de 0 bytes
printf 'aaaaaaaaaa' > "$DATOS/un_simbolo.txt"            # un solo simbolo distinto
printf 'ab' > "$DATOS/dos_simbolos.txt"                  # el arbol mas pequeno posible
printf 'canción, pingüino, ñandú\n' > "$DATOS/tildes.txt" # bytes mayores a 127 (UTF-8)
head -c 200000 /dev/urandom > "$DATOS/aleatorio.bin"     # binario: los 256 valores
for i in $(seq 1 20000); do echo "linea $i del archivo grande"; done > "$DATOS/grande.txt"

# Limites de bloque (HUFF_TAM_BLOQUE = 65536 bytes)
head -c 65536  "$DATOS/grande.txt" > "$DATOS/un_bloque_exacto.txt"   # exactamente 1 bloque
head -c 65537  "$DATOS/grande.txt" > "$DATOS/bloque_mas_uno.txt"     # 1 bloque + 1 byte
head -c 196608 "$DATOS/grande.txt" > "$DATOS/tres_bloques.txt"       # exactamente 3 bloques

# --- Ejecucion ---------------------------------------------------------------------
fallos=0
for original in "$DATOS"/*; do
    nombre=$(basename "$original")
    md5_original=$(md5sum < "$original" | cut -d' ' -f1)
    referencia=""
    estado="OK"

    for h in $HILOS; do
        comprimido="$SALIDA/$nombre.$h.huff"
        recuperado="$SALIDA/$nombre.$h.rec"

        if ! "$HUFF" c "$original" "$comprimido" "$h" 2>/dev/null ||
           ! "$HUFF" d "$comprimido" "$recuperado" "$h" 2>/dev/null; then
            estado="FALLA: el programa retorno error con $h hilo(s)"
            break
        fi

        if [ "$(md5sum < "$recuperado" | cut -d' ' -f1)" != "$md5_original" ]; then
            estado="FALLA: el md5 no coincide con $h hilo(s)"
            break
        fi

        # El .huff debe salir identico con cualquier cantidad de hilos.
        if [ -z "$referencia" ]; then
            referencia="$comprimido"
        elif ! cmp -s "$referencia" "$comprimido"; then
            estado="FALLA: el .huff con $h hilos difiere del de 1 hilo"
            break
        fi
    done

    if [ "$estado" = "OK" ]; then
        printf "[OK]    %-22s (%s -> %s bytes, hilos: %s)\n" "$nombre" \
               "$(stat -c%s "$original")" "$(stat -c%s "$referencia")" "$HILOS"
    else
        echo "[FALLA] $nombre: $estado"
        fallos=$((fallos + 1))
    fi
done

echo
if [ $fallos -eq 0 ]; then
    echo "Todas las pruebas pasaron."
else
    echo "$fallos prueba(s) fallaron."
fi
exit $fallos
