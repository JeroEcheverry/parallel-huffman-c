# parallel-huffman-c

Compresor de archivos con **codificación de Huffman concurrente** en C, integrado como tarea en segundo plano en un editor de texto de línea de comandos.

Proyecto del Parcial 2 de Sistemas Operativos (SO2026B), Universidad EAFIT: concurrencia y sincronización con `pthread`, mutex, variables de condición y cerrojos de lectores-escritores.

## Antecedentes

Este proyecto parte de dos trabajos previos:

Compresor Huffman secuencial en C++, proyecto final de la materia Estructuras de Datos y Algoritmos: JeroEcheverry/Compresor-De-Archivos-Huffman. De ahí se tomaron el algoritmo de construcción del árbol, la generación de códigos y el orden de los bits; aquí se reescribió en C y se paralelizó.
Editor de texto en Unix, Parcial 1 de Sistemas Operativos: JeroEcheverry/Editor-de-texto-en-Unix. Se integró sin cambiar su arquitectura (carpeta editor/).

## Compilar y usar

Requiere Linux (o WSL) con `gcc` y `make`.

```bash
make            # compila ./huff y ./editor_huff
```

### Compresor por línea de comandos

```bash
./huff c <entrada> <salida.huff> [hilos]    # comprimir
./huff d <entrada.huff> <salida> [hilos]    # descomprimir
```

Si no se indica la cantidad de hilos, se usa uno por núcleo. Al terminar se imprime el tiempo en milisegundos.

### Editor con compresión en segundo plano

```bash
./editor_huff notas.txt
```

Además de los comandos del Parcial 1 (`o p a d i s m y x u r h q`), el editor tiene:

| Comando | Qué hace |
|---|---|
| `c [salida.huff]` | Comprime el archivo abierto en segundo plano (por defecto `<archivo>.huff`) |
| `k <entrada.huff> <salida>` | Descomprime en segundo plano. La salida puede ser el archivo abierto: se recarga y se puede deshacer con `u` |
| `e` | Muestra el progreso de la tarea con una barra |
| `e v` | Muestra el progreso en vivo hasta que la tarea termina |
| `Ctrl+C` | Cancela la tarea en segundo plano (no cierra el editor) |

Mientras una tarea corre, el prompt muestra el avance (`ed [c 45%]>`) y el editor sigue respondiendo.

Para ver el avance con archivos pequeños en una demostración, se puede hacer más lento cada bloque:

```bash
HUFF_DEMORA_MS=100 ./editor_huff notas.txt
```

## Probarlo uno mismo

No hace falta correr los scripts para comprobar que funciona. Estos son los pasos a mano.

### 1. Compilar

```bash
git clone https://github.com/JeroEcheverry/parallel-huffman-c.git
cd parallel-huffman-c
make
```

### 2. Comprimir y descomprimir desde el editor

```bash
./editor_huff mis_notas.txt        # si el archivo no existe, se crea
```

Dentro del editor:

```
ed> a Primera linea de prueba
ed> a Segunda linea de prueba
ed> c                              <- comprime a mis_notas.txt.huff en segundo plano
ed>                                <- Enter: aparece "[segundo plano] Compresion terminada"
ed> k mis_notas.txt.huff copia.txt <- descomprime en segundo plano
ed>                                <- Enter: aparece "Descompresion terminada"
ed> o copia.txt
ed> p                              <- debe mostrar las mismas lineas
ed> q
```

Y fuera del editor, la prueba definitiva de que no se perdió ni un byte:

```bash
md5sum mis_notas.txt copia.txt     # los dos hashes deben ser iguales
```

Con un archivo tan pequeño el `.huff` pesa más que el original: el encabezado ocupa unos 2 KB fijos. Con archivos grandes sí se nota la compresión.

### 3. Ver la concurrencia: editar mientras se comprime

```bash
seq 1 3000000 > grande.txt                    # unos 22 MB de texto
HUFF_DEMORA_MS=200 ./editor_huff grande.txt   # la demora hace visible el avance
```

```
ed> c
ed [c 0%]> p 1        <- SE ACEPTA: leer convive con la compresion (lector)
ed [c 5%]> a hola     <- SE RECHAZA: "Archivo ocupado..." (escritor)
ed [c 9%]> e          <- barra de progreso
ed [c 14%]> e v       <- barra en vivo hasta el 100 %
ed> a hola            <- ahora si se acepta
ed> c otra.huff       <- y mientras corre, Ctrl+C: se cancela sin cerrar el editor
ed> q
```

El porcentaje del prompt se actualiza cada vez que se presiona Enter; `e v` lo muestra en vivo.

```bash
ls -l grande.txt grande.txt.huff    # el .huff queda en menos de la mitad
ls otra.huff                        # no existe: la tarea cancelada no deja archivos a medias
```

### Compresión (`huffman/codificar.c`)

1. Se lee el archivo y se divide en bloques de 64 KiB.
2. **En paralelo:** cada hilo cuenta las frecuencias de los bloques que toma, cada bloque en su propia tabla (reducción local, sin mutex).
3. El hilo principal suma las tablas locales (reducción) y construye un único árbol.
4. Se calcula cuánto ocupará cada bloque comprimido y se escribe el encabezado.
5. **En paralelo:** los trabajadores codifican bloques en cualquier orden, mientras el hilo coordinador escribe en el archivo el bloque 0, luego el 1, el 2..., durmiendo en una variable de condición hasta que el siguiente esté listo.

### Descompresión (`huffman/decodificar.c`)

Con la tabla de tamaños del encabezado, cada bloque sabe dónde empieza sin decodificar los anteriores. Los hilos decodifican bloques en paralelo y cada uno escribe en su propia zona de la salida.

### Formato `.huff` (versión 2)

| Campo | Tipo | Bytes |
|---|---|---|
| Firma `"HUF2"` | 4 caracteres | 4 |
| Tamaño original | `uint64_t` | 8 |
| Tamaño de bloque | `uint64_t` | 8 |
| Número de bloques | `uint64_t` | 8 |
| Tabla de frecuencias | `uint64_t[256]` | 2048 |
| Tabla de tamaños | `uint64_t[n]` | 8·n |
| Bloques comprimidos | bytes | variable |

Todos los bloques usan el mismo árbol, pero cada uno empieza en un byte nuevo: así ningún byte de la salida es compartido por dos bloques.


## Pruebas

```bash
make test          # 9 casos (vacío, 1 símbolo, tildes, binario, bordes de bloque...)
                   # con 1, 2, 4 y 8 hilos, comparando md5; además verifica que
                   # el .huff sea idéntico sin importar la cantidad de hilos
make test-editor   # lector permitido y escritor rechazado durante la compresión,
                   # descompresión sobre el archivo abierto, cancelación con q
make tsan          # todo lo anterior compilado con ThreadSanitizer, que reporta
                   # cualquier condición de carrera
make benchmark     # tiempos y speedup con 1, 2, 4 y 8 hilos (archivo de 100 MB)
```

Para revisar fugas de memoria:

```bash
valgrind --leak-check=full ./huff c archivo.txt archivo.huff 4
```

## Decisiones de diseño

- **Bloques de 64 KiB:** suficientes bloques para repartir entre los hilos, con un costo de 8 bytes de tabla y como máximo 7 bits de relleno por bloque.
- **Un solo árbol global:** mejor compresión que un árbol por bloque y una sola tabla de frecuencias en el encabezado.
- **Tabla de frecuencias de tamaño fijo (2 KB):** código más simple; el costo solo se nota en archivos muy pequeños.
- **Reparto dinámico de bloques:** un bloque lento no deja a los demás hilos sin trabajo.
- **Cerrojo de lectura durante toda la compresión:** garantiza que el `.huff` corresponda al archivo tal como estaba al pedir la compresión. La alternativa (comprimir una copia en `/tmp`) permitiría editar durante la compresión, a cambio de duplicar el archivo en disco.
- **Códigos de hasta 64 bits:** un código de 33 bits ya es posible en archivos de unos 9 MB con frecuencias tipo Fibonacci; 64 bits cubren cualquier archivo realista.

## Estructura

```
huffman/   módulo de compresión (frecuencias, árbol, codificar, decodificar, pool, progreso, io)
editor/    editor del Parcial 1 + fondo.c (tareas en segundo plano)
pruebas/   verificar.sh, prueba_editor.sh, benchmark.sh
```

Universidad EAFIT, Sistemas Operativos SO2026B.

