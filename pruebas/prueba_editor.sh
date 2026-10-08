#!/bin/bash
# ====================================================================================
#  prueba_editor.sh  --  Prueba de la integracion del compresor con el editor
# ====================================================================================
#  Le envia comandos al editor por la entrada estandar y revisa su salida.
#  HUFF_DEMORA_MS hace que cada bloque tarde un poco mas, para que la tarea en
#  segundo plano siga corriendo cuando llegan los comandos siguientes.
#
#  Casos:
#    1. Durante la compresion, un comando lector (p) funciona y uno escritor (a)
#       se rechaza con "Archivo ocupado". El editor nunca se congela.
#    2. La compresion termina y el .huff se descomprime identico al original.
#    3. Al terminar la tarea, el archivo vuelve a aceptar modificaciones.
#    4. Descomprimir sobre el archivo abierto lo reemplaza y se puede deshacer.
#    5. Salir (q) con una tarea en curso la cancela y no deja archivos a medias.
#
#  Variable opcional: EDITOR_BIN=<binario> (ej. ./editor_huff_tsan).
# ====================================================================================

DIR=$(cd "$(dirname "$0")" && pwd)
EDITOR_BIN="${EDITOR_BIN:-$DIR/../editor_huff}"
EDITOR_BIN=$(cd "$(dirname "$EDITOR_BIN")" && pwd)/$(basename "$EDITOR_BIN")
TRABAJO="$DIR/salida/editor"

rm -rf "$TRABAJO"
mkdir -p "$TRABAJO"
cd "$TRABAJO" || exit 1

for i in $(seq 1 30000); do echo "linea $i del archivo de prueba del editor"; done > notas.txt
cp notas.txt original.txt

fallos=0
revisar() {   # revisar <descripcion> <comando que debe salir bien>
    if eval "$2"; then
        echo "[OK]    $1"
    else
        echo "[FALLA] $1"
        fallos=$((fallos + 1))
    fi
}

# --- Casos 1, 2 y 3 ------------------------------------------------------------------
# 'e v' muestra el progreso en vivo y retorna cuando la tarea termina.
{
    echo "o notas.txt"
    echo "c notas.huff"
    echo "p 1"
    echo "a esta linea no debe entrar"
    echo "e v"
    echo "e"
    echo "k notas.huff copia.txt"
    echo "e v"
    echo "e"
    echo "a linea agregada despues"
    echo "q"
} | HUFF_DEMORA_MS=50 "$EDITOR_BIN" > sesion1.txt 2>&1

revisar "lector (p) permitido durante la compresion" \
        "grep -q 'linea 1 del archivo' sesion1.txt"
revisar "escritor (a) rechazado durante la compresion" \
        "grep -q \"Archivo ocupado.*'a' modifica\" sesion1.txt"
revisar "compresion terminada en segundo plano" \
        "grep -q 'Compresion terminada' sesion1.txt"
revisar "descompresion terminada en segundo plano" \
        "grep -q 'Descompresion terminada' sesion1.txt"
revisar "el archivo descomprimido es identico al original (md5)" \
        "[ \"\$(md5sum < copia.txt)\" = \"\$(md5sum < original.txt)\" ]"
revisar "despues de la tarea se puede volver a modificar" \
        "[ \"\$(tail -n 1 notas.txt)\" = 'linea agregada despues' ]"

# --- Caso 4: descomprimir sobre el archivo abierto ---------------------------------
cp original.txt abierto.txt
{
    echo "o abierto.txt"
    echo "d 1"
    echo "k notas.huff abierto.txt"
    echo "e v"
    echo "e"
    echo "q"
} | "$EDITOR_BIN" > sesion2.txt 2>&1

revisar "descomprimir sobre el archivo abierto lo recarga" \
        "grep -q 'El archivo abierto se actualizo' sesion2.txt"
revisar "el archivo abierto quedo igual al original" \
        "cmp -s abierto.txt original.txt"

# --- Caso 5: salir con una tarea en curso ------------------------------------------
{
    echo "o notas.txt"
    echo "c cancelado.huff"
    echo "q"
} | HUFF_DEMORA_MS=300 "$EDITOR_BIN" > sesion3.txt 2>&1

revisar "q cancela la tarea en curso" \
        "grep -q 'fue cancelada' sesion3.txt"
revisar "la tarea cancelada no deja un .huff a medias" \
        "[ ! -e cancelado.huff ]"

# Los avisos de ThreadSanitizer (si se usa editor_huff_tsan) salen en la salida.
revisar "sin avisos de ThreadSanitizer" \
        "! grep -q 'WARNING: ThreadSanitizer' sesion1.txt sesion2.txt sesion3.txt"

echo
echo "Sesiones guardadas en $TRABAJO (sesion1.txt, sesion2.txt, sesion3.txt)"
if [ $fallos -eq 0 ]; then
    echo "Todas las pruebas del editor pasaron."
else
    echo "$fallos prueba(s) del editor fallaron."
fi
exit $fallos
