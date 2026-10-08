#!/bin/bash
# Prueba que los archivos se recuperen correctamente con distintas cantidades de hilos.
# HUFF permite elegir otro ejecutable, por ejemplo uno compilado con ThreadSanitizer.

DIR=$(dirname "$0")
DATOS="$DIR/datos"
SALIDA="$DIR/salida"
HUFF="${HUFF:-$DIR/../huff}"
HILOS="1 2 4 8"

mkdir -p "$DATOS" "$SALIDA"

# Prepara archivos de prueba con distintos contenidos y tamanos.
: > "$DATOS/vacio.txt"
printf 'aaaaaaaaaa' > "$DATOS/un_simbolo.txt"
printf 'ab' > "$DATOS/dos_simbolos.txt"
printf 'canción, pingüino, ñandú\n' > "$DATOS/tildes.txt"
head -c 200000 /dev/urandom > "$DATOS/aleatorio.bin"
for i in $(seq 1 20000); do echo "linea $i del archivo grande"; done > "$DATOS/grande.txt"

# Casos que revisan archivos del tamano de un bloque y de varios bloques.
head -c 65536  "$DATOS/grande.txt" > "$DATOS/un_bloque_exacto.txt"
head -c 65537  "$DATOS/grande.txt" > "$DATOS/bloque_mas_uno.txt"
head -c 196608 "$DATOS/grande.txt" > "$DATOS/tres_bloques.txt"

# Comprime y descomprime cada archivo, comparando el resultado.
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
