#!/bin/bash
# ====================================================================================
#  verificar.sh  --  Prueba de integridad del compresor Huffman
# ====================================================================================
#  Por cada archivo de pruebas/datos: lo comprime, lo descomprime y compara el
#  resultado con el original usando md5sum. Si los hashes coinciden, el archivo
#  se recupero sin perder ni un byte.
#
#  Los archivos de prueba se generan aqui mismo para cubrir los casos limite.
# ====================================================================================

DIR=$(dirname "$0")
DATOS="$DIR/datos"
SALIDA="$DIR/salida"
HUFF="$DIR/../huff"

mkdir -p "$DATOS" "$SALIDA"

# --- Casos de prueba ---------------------------------------------------------------
: > "$DATOS/vacio.txt"                                   # archivo de 0 bytes
printf 'aaaaaaaaaa' > "$DATOS/un_simbolo.txt"            # un solo simbolo distinto
printf 'ab' > "$DATOS/dos_simbolos.txt"                  # el arbol mas pequeno posible
printf 'canción, pingüino, ñandú\n' > "$DATOS/tildes.txt" # bytes mayores a 127 (UTF-8)
head -c 200000 /dev/urandom > "$DATOS/aleatorio.bin"     # binario: los 256 valores
for i in $(seq 1 20000); do echo "linea $i del archivo grande"; done > "$DATOS/grande.txt"

# --- Ejecucion ---------------------------------------------------------------------
fallos=0
for original in "$DATOS"/*; do
    nombre=$(basename "$original")
    comprimido="$SALIDA/$nombre.huff"
    recuperado="$SALIDA/$nombre.rec"

    if ! "$HUFF" c "$original" "$comprimido" || ! "$HUFF" d "$comprimido" "$recuperado"; then
        echo "[FALLA] $nombre: el programa retorno error"
        fallos=$((fallos + 1))
        continue
    fi

    md5_original=$(md5sum < "$original" | cut -d' ' -f1)
    md5_recuperado=$(md5sum < "$recuperado" | cut -d' ' -f1)

    if [ "$md5_original" = "$md5_recuperado" ]; then
        echo "[OK]    $nombre  ($(stat -c%s "$original") -> $(stat -c%s "$comprimido") bytes)"
    else
        echo "[FALLA] $nombre: el md5 no coincide"
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