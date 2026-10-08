#!/bin/bash
# Prueba las tareas en segundo plano y la compresion desde el editor.
# EDITOR_BIN permite elegir otro ejecutable, por ejemplo uno con ThreadSanitizer.

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

# Prueba el acceso al archivo durante una tarea y la compresion/descompresion.
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

# Prueba reemplazar el archivo abierto y verificar su contenido.
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

# Prueba que al salir se cancele una tarea que sigue activa.
{
    echo "o notas.txt"
    echo "c cancelado.huff"
    echo "q"
} | HUFF_DEMORA_MS=300 "$EDITOR_BIN" > sesion3.txt 2>&1

revisar "q cancela la tarea en curso" \
        "grep -q 'fue cancelada' sesion3.txt"
revisar "la tarea cancelada no deja un .huff a medias" \
        "[ ! -e cancelado.huff ]"

# Revisa que las sesiones no hayan generado avisos de ThreadSanitizer.
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
