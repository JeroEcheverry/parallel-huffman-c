#!/bin/bash
# ====================================================================================
#  benchmark.sh  --  Tiempos de compresion y descompresion segun la cantidad de hilos
# ====================================================================================
#  Genera un archivo de texto grande (unos 100 MB) y lo comprime y descomprime con
#  1, 2, 4 y 8 hilos. Cada medicion se repite 3 veces y se toma la mejor.
#  El speedup es: tiempo con 1 hilo / tiempo con N hilos.
#
#  Variable opcional: TAM_MB=<megas> para cambiar el tamano del archivo.
# ====================================================================================

DIR=$(dirname "$0")
HUFF="$DIR/../huff"
TRABAJO="$DIR/salida/benchmark"
TAM_MB="${TAM_MB:-100}"
ARCHIVO="$TRABAJO/grande.txt"

mkdir -p "$TRABAJO"

if [ ! -f "$ARCHIVO" ] || [ "$(stat -c%s "$ARCHIVO")" -lt $((TAM_MB * 1000000)) ]; then
    echo "Generando archivo de prueba de ${TAM_MB} MB..."
    # Texto con palabras repetidas: se parece a un documento real y se comprime.
    yes "El sistema operativo administra procesos, hilos, memoria y archivos. 0123456789" \
        | head -c $((TAM_MB * 1000000)) > "$ARCHIVO"
fi

echo "Archivo: $(stat -c%s "$ARCHIVO") bytes, nucleos disponibles: $(nproc)"
echo

# mejor_tiempo <c|d> <entrada> <salida> <hilos>: imprime el menor tiempo en ms
mejor_tiempo() {
    local mejor=""
    for _ in 1 2 3; do
        local t
        t=$("$HUFF" "$1" "$2" "$3" "$4" 2>&1 | sed -n 's/.*: \([0-9.]*\) ms/\1/p')
        if [ -z "$mejor" ] || awk "BEGIN{exit !($t < $mejor)}"; then
            mejor=$t
        fi
    done
    echo "$mejor"
}

printf "| Hilos | Compresion (ms) | Speedup | Descompresion (ms) | Speedup |\n"
printf "|------:|----------------:|--------:|-------------------:|--------:|\n"

base_c=""
base_d=""
for h in 1 2 4 8; do
    tc=$(mejor_tiempo c "$ARCHIVO" "$TRABAJO/grande.huff" "$h")
    td=$(mejor_tiempo d "$TRABAJO/grande.huff" "$TRABAJO/grande.rec" "$h")
    [ -z "$base_c" ] && base_c=$tc && base_d=$td
    printf "| %5s | %15s | %7s | %18s | %7s |\n" "$h" "$tc" \
           "$(awk "BEGIN{printf \"%.2fx\", $base_c/$tc}")" "$td" \
           "$(awk "BEGIN{printf \"%.2fx\", $base_d/$td}")"
done

echo
if cmp -s "$ARCHIVO" "$TRABAJO/grande.rec"; then
    echo "Integridad: el archivo recuperado es identico al original."
else
    echo "ERROR: el archivo recuperado no coincide con el original."
    exit 1
fi
