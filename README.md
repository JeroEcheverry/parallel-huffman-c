# parallel-huffman-c

Compresor de archivos con **codificación de Huffman concurrente** en C (hilos POSIX), integrado como tarea en segundo plano en un editor de texto de línea de comandos.

Proyecto del Parcial 2 de Sistemas Operativos (SO2026B), Universidad EAFIT: concurrencia y sincronización con `pthread`, mutex, variables de condición y cerrojos de lectores-escritores.

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

## Arquitectura

```
                     editor_huff
 ┌─────────────────────────────────────────────────────┐
 │ Hilo principal (REPL)                               │
 │   lee comandos; nunca se bloquea por el archivo     │
 │   (tryrdlock / trywrlock)                           │
 │        │ c / k                                      │
 │        ▼                                            │
 │ Hilo de la tarea (fondo.c)                          │
 │   toma el cerrojo del archivo y llama al módulo     │
 │        │                                            │
 │        ▼                                            │
 │ Módulo Huffman                                      │
 │   pool de N hilos trabajadores (pool.c)             │
 │   + hilo coordinador que escribe en orden           │
 └─────────────────────────────────────────────────────┘
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

## Protocolo de sincronización

| Recurso compartido | Primitiva | Quiénes lo usan | Por qué |
|---|---|---|---|
| Contador de la siguiente tarea del pool | `pthread_mutex_t` | Trabajadores | Dos hilos no deben tomar el mismo bloque |
| `listo` de cada bloque y `error` (compresión) | `pthread_mutex_t` + `pthread_cond_t bloque_listo` | Trabajadores y coordinador | El coordinador duerme hasta que el bloque que le toca esté listo; escribe en orden sin espera activa |
| Progreso (`hechos`, `total`, `terminado`, `cancelar`) | `pthread_mutex_t` + `pthread_cond_t cambio` | Trabajadores, coordinador y editor | `e v` duerme hasta que el progreso cambie (`pthread_cond_timedwait`) |
| Archivo abierto en el editor y su índice de líneas | `pthread_rwlock_t` | Hilo principal y tarea | Lectores-escritores: comprimir lee; editar y descomprimir sobre el archivo escriben |
| Estado de la tarea (`arrancado`, `terminada`, `resultado`) | `pthread_mutex_t` + `pthread_cond_t estado` | Tarea y hilo principal | El editor espera a que la tarea tome su cerrojo antes de aceptar el siguiente comando |
| Tablas de frecuencias por bloque y zonas de salida por bloque | ninguna | Un solo hilo cada una | Datos particionados: no hay memoria compartida que proteger |

**Lectores-escritores en el editor.** Cada comando declara en la tabla de `repl.c` si lee (`p s m y`) o modifica (`a d i x u r o`) el archivo. Mientras se comprime, la tarea tiene el cerrojo de lectura: los lectores siguen funcionando y los escritores se rechazan con un mensaje. El hilo principal usa `pthread_rwlock_tryrdlock`/`trywrlock`, que retornan `EBUSY` de inmediato en lugar de dormir, así la interfaz nunca se congela.

**Sin espera activa.** Todos los puntos donde un hilo espera a otro usan `pthread_cond_wait`, `pthread_cond_timedwait` o `pthread_join`; ningún hilo pregunta en un ciclo.

**Sin interbloqueos.** Nunca se toma un mutex mientras se espera otro, salvo en un solo orden: el cerrojo del archivo antes de los mutex internos del compresor. El hilo principal nunca se bloquea esperando el cerrojo del archivo (usa *try*), y solo hace `pthread_join` cuando la tarea ya terminó o fue cancelada.

**Señales.** `SIGINT` (Ctrl+C) se bloquea en todos los hilos de trabajo (heredan la máscara al crearse), así que solo lo recibe el hilo principal. El manejador solo enciende una bandera de tipo `sig_atomic_t`; el bucle del editor la atiende y cancela la tarea.

**Recursos.** Cada función usa el patrón `goto fin`: todos los recursos empiezan vacíos y en `fin` se libera solo lo que se alcanzó a reservar. Si una compresión falla o se cancela, se borra el archivo de salida incompleto. Al salir, el editor cancela la tarea, le hace `pthread_join` y destruye mutex, condiciones y cerrojos.

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

## Rendimiento

Resultado de `make benchmark` en la máquina de pruebas:

> Pega aquí la tabla que imprime `make benchmark`, indicando procesador y número de núcleos.

El speedup no crece linealmente con los hilos: la construcción del árbol, la escritura del archivo y la reducción de frecuencias son secuenciales (ley de Amdahl), y con más hilos que núcleos solo se agrega costo de cambio de contexto.

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

## Equipo

Universidad EAFIT, Sistemas Operativos SO2026B.

- Jerónimo Echeverry ([@JeroEcheverry](https://github.com/JeroEcheverry))
