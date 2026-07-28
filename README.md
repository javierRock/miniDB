# MiniDB

Mini sistema gestor de base de datos persistente escrito en C++20. Almacena los
datos en un único archivo binario organizado en páginas de tamaño fijo, los
administra en RAM mediante un Buffer Pool con reemplazo LRU, indexa la clave
primaria con un hash persistente mapeado sobre las mismas páginas, y procesa
consultas SQL con un parser propio y operadores físicos que siguen el modelo
Volcano (`Open`, `Next`, `Close`).

No pretende competir con PostgreSQL, MySQL ni SQLite: es un sistema pequeño,
académico y demostrable. Todo lo que hay aquí está implementado desde cero sobre
la biblioteca estándar de C++; la única dependencia externa es GoogleTest, y
solo para las pruebas.

## Índice

- [Características](#características)
- [Requisitos](#requisitos)
- [Compilación](#compilación)
- [Ejecución](#ejecución)
- [Pruebas](#pruebas)
- [SQL soportado](#sql-soportado)
- [Arquitectura](#arquitectura)
- [Diseño de páginas](#diseño-de-páginas)
- [Buffer Pool](#buffer-pool)
- [Índice hash](#índice-hash)
- [Modelo Volcano](#modelo-volcano)
- [Archivos en disco](#archivos-en-disco)
- [Limitaciones](#limitaciones)

El detalle técnico completo —offsets, invariantes y diagramas— está en
[ARCHITECTURE.md](ARCHITECTURE.md).

## Características

- **Persistencia real**: los datos viven en `data/minidb.db`, en páginas de 4096
  bytes. Al cerrar y reabrir el programa siguen ahí.
- **Gestor de páginas**: `DiskManager` lee y escribe páginas completas, asigna y
  libera identificadores, y mantiene una lista de páginas libres para reutilizar
  el espacio.
- **Slotted pages**: los registros de longitud variable se organizan con un
  directorio de slots, con compactación y reutilización del espacio borrado.
- **Buffer Pool**: número fijo de frames en RAM, page table para búsqueda O(1),
  contador de fijaciones, marca de página sucia y reemplazo LRU.
- **Índice hash persistente**: la clave primaria se indexa en páginas físicas
  que se leen y escriben a través del Buffer Pool, con resolución de colisiones
  por encadenamiento y páginas de overflow.
- **Procesamiento de consultas**: tokenizador y parser descendente recursivo
  propios, y cuatro operadores físicos componibles.

## Requisitos

| Herramienta | Versión mínima | Notas |
|---|---|---|
| Compilador C++20 | GCC 12 o Clang 15 | Desarrollado con GCC 16 |
| CMake | 3.20 | |
| Ninja | cualquiera | Opcional, pero es el generador usado aquí |
| GoogleTest | 1.10 | Solo para las pruebas |

En Arch Linux y CachyOS:

```bash
sudo pacman -S --needed base-devel cmake ninja gtest
```

En Debian o Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build libgtest-dev
```

## Compilación

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j"$(nproc)"
```

Compilación con sanitizadores de direcciones y de comportamiento indefinido.
Merece la pena porque toda la capa de almacenamiento manipula offsets crudos
dentro de búferes de 4096 bytes:

```bash
cmake -S . -B build-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMINIDB_ENABLE_SANITIZERS=ON
cmake --build build-sanitize -j"$(nproc)"
ctest --test-dir build-sanitize --output-on-failure
```

## Ejecución

Sesión interactiva:

```bash
./build/minidb data/minidb.db
```

```
MiniDB iniciado correctamente
Archivo: /home/usuario/mini-dbms/data/minidb.db
Tamaño de página: 4096 bytes
Frames del Buffer Pool: 8
Base de datos vacía: use CREATE TABLE para empezar
Escriba .help para ver la ayuda

minidb> SELECT * FROM students WHERE age >= 20;
Plan físico (modelo Volcano): ProjectionOperator <- FilterOperator <- SequentialScanOperator
+----+------+-----+---------------------------+
| id | name | age | career                    |
+----+------+-----+---------------------------+
| 1  | Ana  | 21  | Ciencia de la Computación |
| 2  | Luis | 22  | Ingeniería de Sistemas    |
+----+------+-----+---------------------------+
2 filas devueltas
```

Ejecución de un guion:

```bash
./build/minidb data/minidb.db < examples/demo.sql
```

Demostración completa, que además **cierra y reabre** la base para probar la
persistencia y comprueba que el archivo no crece tras un ciclo de borrado y
reinserción:

```bash
bash scripts/demo.sh
```

Evidencia de los archivos en disco (tamaños, `stat`, `file` y volcado
hexadecimal de las cabeceras):

```bash
bash scripts/inspect_database.sh
```

### Comandos internos

| Comando | Qué muestra |
|---|---|
| `.help` | SQL admitido, comandos y ejemplos |
| `.schema` | Esquema de la tabla y número de registros |
| `.pages` | Páginas del archivo, por tipo, y la lista de libres |
| `.buffer` | Estado de cada frame y estadísticas del Buffer Pool |
| `.files` | Rutas y tamaños de los archivos en disco |
| `.flush` | Sincroniza todas las páginas sucias |
| `.exit` | Sincroniza y sale |

## Pruebas

```bash
ctest --test-dir build --output-on-failure
```

Cerca de 200 casos repartidos en ocho binarios, uno por componente. Cada prueba
usa su propio archivo temporal y lo borra al terminar, así que no comparten
estado y pueden ejecutarse en cualquier orden.

| Binario | Cubre |
|---|---|
| `serialization_test` | Orden de bytes, límites, cadenas UTF-8 |
| `disk_manager_test` | Persistencia, validación de cabecera, lista de libres |
| `table_page_test` | Slotted page, compactación, capacidad exacta, registros |
| `buffer_pool_test` | LRU, fijaciones, escritura de páginas sucias, `PageGuard` |
| `table_heap_test` | Cadena de páginas, reubicación, no crecimiento del archivo |
| `hash_index_test` | Colisiones, overflow, inserción masiva, persistencia |
| `parser_test` | Gramática, errores con posición, mayúsculas, acentos |
| `volcano_test` | `Open`/`Next`/`Close`, elección de plan, streaming |
| `integration_test` | Ciclo completo, consistencia tabla-índice, configuración |

## SQL soportado

```sql
CREATE TABLE <tabla> (<col> INT [PRIMARY KEY] | <col> VARCHAR(<n>), ...);
INSERT INTO <tabla> VALUES (<v1>, <v2>, ...);
SELECT * | <col>, ... FROM <tabla> [WHERE <col> <op> <valor>];
UPDATE <tabla> SET <col> = <valor> [, ...] [WHERE <col> <op> <valor>];
DELETE FROM <tabla> [WHERE <col> <op> <valor>];
```

- Operadores de comparación: `=`, `!=` (o `<>`), `<`, `<=`, `>`, `>=`.
- Las palabras clave no distinguen mayúsculas de minúsculas; los identificadores
  conservan su forma pero se comparan sin distinguirlas.
- El `;` final es opcional en modo interactivo y separa sentencias en un guion.
- Tipos: `INT` (`std::int32_t`) y `VARCHAR(n)`, con `n` entre 1 y 255 **bytes**
  UTF-8. Esto importa con datos en español: `'Ciencia de la Computación'` son 25
  caracteres pero 26 bytes.
- Las cadenas van entre comillas simples; `''` dentro de una cadena es una
  comilla literal.

## Arquitectura

```
CLI (main.cpp)
      |
   Database  -- fachada: abre el archivo y ensambla el resto
      |
  Parser + ExecutionEngine
      |
  Operadores físicos (Volcano)
      |
  TableHeap  +  HashIndex
      |
  BufferPoolManager  (frames, LRU, páginas sucias)
      |
  DiskManager        (páginas de 4096 bytes, lista de libres)
      |
  data/minidb.db
```

Las reglas de dependencia que sostienen el diseño:

- `TableHeap`, `HashIndex` y `Catalog` solo conocen el `BufferPoolManager`.
  Ninguno tiene acceso al `DiskManager` ni a un archivo.
- La página 0 pertenece en exclusiva al `DiskManager`: guarda los metadatos del
  asignador, y mantenerla privada evita una dependencia circular entre el
  asignador y el Buffer Pool.
- El parser no incluye nada de `storage/`, `buffer/` ni `index/`: convierte
  texto en un `Statement` y nada más.
- Los operadores no conocen el Buffer Pool ni el disco.
- `main.cpp` solo habla con `Database`.

## Diseño de páginas

Todas las páginas miden 4096 bytes y su offset es `page_id * 4096`. Una página
de tabla se organiza así:

```
0        12                 free_space_begin      free_space_end      4096
|--------|------------------|---------------------|-------------------|
 cabecera   directorio slots      espacio libre      datos de registros
            (crece ->)                                  (<- crece)
```

La cabecera ocupa 12 bytes y cada slot 4. `free_space_begin` no se almacena: es
siempre `12 + 4 * slot_count`, y un campo redundante es un invariante más que
puede desincronizarse.

Un slot cuyo offset vale 0 está libre. El offset 0 es imposible para datos
reales porque la cabecera ocupa los bytes 0..11, así que sirve de centinela y
ahorra un byte de estado por slot.

Con el esquema de demostración, un registro ocupa como máximo 112 bytes, de modo
que en una página caben exactamente 35 registros en el peor caso
(`12 + 35*4 + 35*112 = 4072`) y no 36 (`4188 > 4096`).

**Reutilización del espacio.** Al borrar, el slot queda disponible para la
siguiente inserción. Al actualizar un registro que crece, se intenta primero en
el sitio, después en el hueco libre, después compactando, y solo si nada de eso
basta se reubica en otra página. `Compact()` conserva los identificadores de
slot, lo cual es imprescindible: el índice apunta a pares
`(página, slot)`, y renumerarlos dejaría todas sus entradas colgando.

## Buffer Pool

Ocho frames por defecto, configurables en `minidb.conf`. La operación más
costosa —un `UPDATE` que reubica un registro— fija seis páginas a la vez, así
que ocho dejan margen.

- Una `page table` (`unordered_map`) resuelve la búsqueda en O(1).
- `pin_count` cuenta los usuarios activos de una página. Una página fijada nunca
  se reemplaza: sencillamente no está en la estructura del reemplazador.
- La marca de página sucia solo se borra **después** de que la escritura a disco
  haya tenido éxito, y se acumula entre desfijaciones, para que un lector que
  desfija limpiamente no borre el hecho de que otro la modificó.
- `PageGuard` desfija en su destructor. Una fijación perdida retira un frame de
  un conjunto muy pequeño, y unas sentencias después el sistema se queda sin
  víctimas; una excepción a mitad de operación lo provocaría en silencio.

## Índice hash

La clave primaria se mapea a la posición del registro. La clase **no guarda
ninguna entrada en RAM**: sus únicos miembros son una referencia al Buffer Pool
y el identificador de su página de cabecera, así que cada búsqueda, inserción y
borrado lee y escribe páginas reales.

- 16 buckets, con sus páginas asignadas por adelantado.
- Cada página de bucket almacena hasta 408 entradas de 10 bytes.
- Las colisiones se resuelven por encadenamiento: cuando una página se llena, se
  enlaza detrás una página de overflow.
- El borrado mueve la última entrada al hueco en lugar de dejar una lápida, así
  que las páginas no se degradan con agujeros. Una página de overflow que se
  vacía se desenlaza y vuelve a la lista de páginas libres del archivo.
- La función hash es propia (multiplicativa de Knuth), no `std::hash`: esta
  última está definida por la implementación —es la identidad en libstdc++—, de
  modo que una base creada con una biblioteca estándar no podría leerse con
  otra.

## Modelo Volcano

Cada operador es un iterador: `Open` lo prepara, `Next` produce un registro por
llamada y devuelve `std::nullopt` al agotarse, y `Close` libera lo que `Open`
tomó. Los operadores se componen sujetando un hijo y tirando de él, de modo que
un plan es un árbol que va pasando registros hacia arriba sin materializar
nunca el resultado completo.

```
SELECT * FROM students;              SELECT * FROM students WHERE id = 1;

  ProjectionOperator                   ProjectionOperator
          |                                    |
  SequentialScanOperator               IndexScanOperator


SELECT * FROM students WHERE age >= 20;

  ProjectionOperator
          |
  FilterOperator
          |
  SequentialScanOperator
```

El planificador elige `IndexScanOperator` **solo** cuando la condición es una
igualdad sobre la clave primaria, que es la única pregunta que un índice hash
sabe responder. `WHERE id > 1` recae en escaneo más filtro aunque `id` sea la
clave.

`UPDATE` y `DELETE` no son operadores: el modelo Volcano describe iteradores que
producen tuplas, y una sentencia cuya única salida es un contador no encaja en
esa forma. Sí construyen un plan real para localizar sus filas, en dos fases:
primero recorren el plan y recogen las posiciones, lo cierran, y solo entonces
modifican. Reescribir páginas mientras un escaneo sigue recorriéndolas
invalidaría los offsets de los que depende.

## Archivos en disco

| Archivo | Qué es |
|---|---|
| `data/minidb.db` | Base de datos completa: cabecera, catálogo, datos, índice y overflow |
| `minidb.conf` | Configuración en texto: archivo de datos y número de frames |

El sistema no genera archivos temporales, porque no implementa operadores
bloqueantes (`ORDER BY`, `GROUP BY`, `JOIN`) que necesiten volcar resultados
intermedios.

Tras crear la tabla, el archivo ocupa exactamente 20 páginas (81 920 bytes): la
cabecera, el catálogo, la cabecera del índice, los 16 buckets y la primera
página de datos. Los cuatro primeros bytes son `MIND`, de modo que el archivo se
identifica a simple vista con `xxd`.

## Limitaciones

Conocidas y asumidas; están fuera del alcance del trabajo:

1. **Una sola tabla de usuario.** El catálogo guarda un esquema.
2. **Sin recuperación ante fallos.** No hay registro de escritura anticipada
   (WAL). Un cierre limpio, `.exit` y `.flush` sincronizan; una terminación
   abrupta puede perder las últimas sentencias no sincronizadas.
3. **Sin transacciones ni concurrencia.** Un proceso, un hilo.
4. **El índice solo acelera la igualdad sobre la clave primaria.** Los rangos y
   las columnas no indexadas usan escaneo secuencial.
5. **Número de buckets fijo.** No hay hash extensible ni redistribución; el
   crecimiento se absorbe con páginas de overflow.
6. **Sin `JOIN`, `GROUP BY`, `ORDER BY`, subconsultas, `AND`/`OR`, `ALTER TABLE`
   ni `NULL`.**
7. **No se permite actualizar la clave primaria**, para no tener que reescribir
   entradas de índice bajo una clave distinta.
8. **`VARCHAR` compara bytes**, no aplica reglas lingüísticas de ordenación.
