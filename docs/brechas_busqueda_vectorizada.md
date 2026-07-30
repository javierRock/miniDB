# Informe de brechas: búsqueda vectorizada por similitud

**Proyecto:** Mini-SGBD (`/home/javier/Documentos/BDII/mini-dbms`)
**Commit inspeccionado:** `7f6f8d4` — *feat: add vectorized batch-at-a-time execution alongside the Volcano path*
**Fecha de inspección:** [COMPLETAR FECHA DE EJECUCIÓN]
**Motivo del informe:** puerta de validación §7 del prompt maestro.

---

## 1. Veredicto

No se encontró evidencia suficiente en el repositorio para afirmar que la búsqueda
vectorizada por similitud entre vectores numéricos se encuentra implementada.

En consecuencia, y siguiendo la regla §7 del prompt maestro, **no se ha redactado el
artículo científico**. Redactarlo exigiría inventar un tipo de dato vectorial,
métricas de distancia, un ejecutor de vecinos más cercanos y resultados
experimentales que el sistema no puede producir hoy.

---

## 2. Origen de la confusión: dos conceptos distintos con el mismo nombre

El repositorio **sí** contiene una característica llamada «vectorizada», pero es la
otra acepción del término, y el prompt maestro (§3) advierte expresamente de no
confundirlas.

| | Implementado en el repositorio | Solicitado por el prompt |
|---|---|---|
| **Nombre** | Ejecución vectorizada de operadores | Búsqueda vectorizada por similitud |
| **Qué agrupa** | Lotes de ~1024 **registros** por llamada a `NextBatch` | Vectores numéricos (*embeddings*) como **dato** |
| **Qué resuelve** | Coste de interpretación del plan y accesos al Buffer Pool | Recuperar los `k` registros más parecidos a un vector de consulta |
| **Operación central** | Comparación SIMD de una columna `INT` contra un literal | Cálculo de distancia entre dos vectores de dimensión `d` |
| **Salida** | Las mismas filas que el camino tupla a tupla, más rápido | Un ranking de `k` vecinos ordenado por distancia |
| **Evidencia** | `src/execution/vectorized_operators.cpp`, clases `VectorizedScanOperator` y `VectorizedFilterOperator` | Ninguna |
| **Literatura** | MonetDB/X100, Vectorwise | FAISS, HNSW, IVF, pgvector |

Ambas son legítimas y ambas se llaman «vectorización» en la literatura, pero
responden a preguntas distintas: la primera es una técnica de **ejecución**, la
segunda es un **tipo de consulta**. Un artículo sobre la segunda no puede
sostenerse con la evidencia de la primera.

---

## 3. Puerta de validación (§7 del prompt maestro)

Estado verificado componente por componente. «No verificable» no se usa en ningún
caso: todos los componentes pudieron comprobarse leyendo el código y compilando.

| Componente | Estado | Evidencia |
|---|---|---|
| Gestor de almacenamiento | **Implementado** | `src/storage/disk_manager.cpp`, clase `DiskManager` |
| Administrador de páginas | **Implementado** | `src/storage/table_page.cpp`, clase `TablePage`; páginas fijas de 4096 B |
| Administrador de buffer | **Implementado** | `src/buffer/buffer_pool_manager.cpp`, clase `BufferPoolManager`; reemplazo LRU en `src/buffer/lru_replacer.cpp` |
| Catálogo | **Implementado** | `src/catalog/catalog.cpp`, clase `Catalog`; persistido en la página 1 |
| Tipo de dato vectorial | **Ausente** | `include/minidb/common/types.hpp` línea 42: `enum class ColumnType { kInteger = 1, kVarchar = 2 }`. No existe ningún tercer tipo |
| Serialización de vectores | **Ausente** | `src/storage/record.cpp`, función `Record::SerializeTo`, solo contempla `INT` (4 B) y `VARCHAR` (2 B de longitud + bytes) |
| Persistencia de vectores | **Ausente** | Consecuencia de las dos anteriores |
| Métricas de similitud | **Ausente** | `grep -rniE "euclid\|coseno\|cosine\|distance\|similarity\|dot_product\|norm("` sobre `include/`, `src/` y `tests/` no devuelve ninguna coincidencia |
| Consulta k-NN | **Ausente** | `grep -rniE "knn\|nearest\|vecino\|top_?k"` no devuelve ninguna coincidencia |
| Búsqueda secuencial | **Implementado, pero no vectorial** | `SequentialScanOperator` y `VectorizedScanOperator` recorren la tabla, y `FilterOperator` evalúa predicados de comparación escalar. No existe recorrido que calcule distancias |
| Índice vectorial | **Ausente** | El único índice es `src/index/hash_index.cpp`, clase `HashIndex`, que mapea `std::int32_t → RecordId` por igualdad exacta. Un índice hash no puede responder consultas de proximidad |
| Selector Top-k | **Ausente** | `src/execution/blocking_operators.cpp`, clase `SortOperator`, ordena **todo** su entrada con `std::stable_sort`; no existe cola de prioridad acotada ni `LIMIT` |
| Instrumentación de tiempo | **Implementado** | `src/database/database.cpp`, función `Database::Execute`, mide con `std::chrono::steady_clock` y expone `QueryResult::elapsed_ms` |
| Instrumentación de hits y misses | **Implementado** | `include/minidb/buffer/buffer_pool_manager.hpp` línea 30, `struct BufferPoolStatistics { hits, misses, evictions, disk_reads, disk_writes }` |
| Pruebas automatizadas | **Implementado** | 264 casos en 11 binarios; `ctest --test-dir build-release` en verde |
| Benchmarks | **Implementado, pero de otra cosa** | `scripts/benchmark.sh`, y los comandos `.bench` y `.benchvec` en `src/main.cpp`. Comparan índice hash activado/desactivado y los dos modelos de ejecución. Ninguno mide similitud vectorial |

**Resumen:** 8 componentes implementados, 7 ausentes, 1 implementado pero ajeno al
objetivo. Los siete ausentes son exactamente los que constituyen la característica
solicitada.

---

## 4. Evidencia de la ausencia

Comandos ejecutados y su resultado literal, para que la afirmación sea
reproducible y no una opinión.

```console
$ grep -rn "float" include src tests
NINGUNA APARICIÓN DE 'float' EN include/ src/ tests/

$ grep -rniE "euclid|coseno|cosine|distancia|distance|similitud|similarity|dot_product|norm\(" include src tests
NINGUNA APARICIÓN

$ grep -rniE "knn|k-nn|vecino|nearest|embedding|VECTOR\(|top_?k|topk" include src tests
NINGUNA APARICIÓN
```

El sistema no maneja ningún número en punto flotante en ninguna parte: el único
tipo numérico persistido es `std::int32_t`.

```cpp
// include/minidb/common/value.hpp, línea 15
using Value = std::variant<std::int32_t, std::string>;
```

Las apariciones de la palabra «vector» en el código corresponden a
`std::vector<T>` de la biblioteca estándar y a los operadores de ejecución por
lotes descritos en la sección 2.

---

## 5. Qué impide realizar los experimentos solicitados

Aunque se aceptara redactar el artículo sobre lo ya existente, el diseño
experimental del prompt (§11 y §12) no podría ejecutarse. Los obstáculos son de
tres clases.

### 5.1 No hay nada que medir

Ninguna de las comparaciones principales admitidas en §4.3 puede ejecutarse hoy
en términos vectoriales:

| Comparación pedida | Estado |
|---|---|
| Búsqueda secuencial frente a índice vectorial | Imposible: falta la búsqueda vectorial **y** el índice vectorial |
| Búsqueda exacta frente a búsqueda aproximada | Imposible: no existe ninguna de las dos |
| Ordenamiento completo frente a selección Top-k | Imposible sobre vectores; `SortOperator` existe pero no hay selector Top-k ni distancias |
| Ejecución sin buffer frente a ejecución con buffer | Imposible: el Buffer Pool no se puede desactivar por diseño; ninguna capa por encima puede llamar al `DiskManager` (invariante arquitectónico verificado en CI) |
| Índice activado frente a índice desactivado | **Ejecutable**, pero sobre el índice hash de la clave primaria, no sobre vectores (`.bench`, `Database::SetIndexEnabled`) |
| Implementación original frente a vectorizada optimizada | **Ejecutable**, pero es ejecución por lotes de operadores, no similitud vectorial (`.benchvec`) |

### 5.2 Los volúmenes pedidos exceden el formato binario actual

El prompt pide hasta 500 000 vectores de hasta 768 dimensiones. Sobre el formato
físico real del sistema, esas cifras implican:

| Dimensión `d` | Bytes por vector (`2 + 4d`) | Registros por página de 4096 B | Páginas para 100 000 vectores | Tamaño del archivo |
|---:|---:|---:|---:|---:|
| 16 | 66 | 58 | 1 725 | 6.7 MiB |
| 64 | 258 | 15 | 6 667 | 26 MiB |
| 128 | 514 | 7 | 14 286 | 56 MiB |
| 256 | 1 026 | 3 | 33 334 | 130 MiB |
| 768 | 3 074 | **1** | 100 000 | **391 MiB** |

Cálculo: una página de tabla ofrece `4096 − 12 = 4084` bytes para directorio y
datos, y cada registro consume su tamaño más 4 bytes de *slot*
(`include/minidb/storage/table_page.hpp`, constantes `kHeaderSize` y `kSlotSize`).

Consecuencias concretas:

1. Con `d = 768` cabe **un solo vector por página**, de modo que el escaneo
   secuencial degenera en una lectura de página por registro y el experimento
   mediría sobre todo el subsistema de archivos, no la búsqueda.
2. Con 500 000 vectores de 768 dimensiones el archivo rondaría **1.9 GiB**, muy
   por encima de lo que este sistema —de un solo hilo, sin lectura anticipada y
   sin E/S asíncrona— puede recorrer en un tiempo razonable por consulta.
3. La cota `kMaxVarcharLength = 255`
   (`include/minidb/common/constants.hpp`, línea 31) impide reutilizar `VARCHAR`
   como contenedor: 255 bytes solo alojarían 63 valores `float`.
4. La cota `kMaxColumns = 8` (misma línea 30) no es un problema, pero el ancho
   máximo declarable por columna del catálogo sí debe revisarse: `max_length` se
   persiste como `std::uint16_t`, lo que limita un vector a 16 383 dimensiones
   —suficiente, pero es una decisión que debe documentarse.

**Recomendación:** acotar los experimentos a `d ∈ {16, 32, 64, 128}` y
`n ∈ {1 000, 10 000, 50 000, 100 000}`, y justificar en el artículo la desviación
respecto de los valores orientativos del prompt con la tabla anterior. Un artículo
honesto explica por qué eligió su rango; no finge cubrir uno que su sistema no
soporta.

### 5.3 Falta instrumentación específica

Lo que ya existe y sirve tal cual:

| Métrica | Dónde | Evidencia |
|---|---|---|
| Latencia por sentencia | `QueryResult::elapsed_ms` | `src/database/database.cpp`, función `Database::Execute` |
| Hits, misses de buffer | `QueryResult::buffer_hits`, `buffer_misses` | ídem, por diferencia de `BufferPoolStatistics` |
| Lecturas físicas de disco | `QueryResult::pages_read` (delta de `disk_reads`) | `src/buffer/buffer_pool_manager.cpp` |
| Escrituras de disco, desalojos | `BufferPoolStatistics::disk_writes`, `evictions` | ídem |
| Registros examinados por operador | `OperatorMetrics::rows_produced` | `include/minidb/execution/physical_operator.hpp`, método `Counted` |
| Llamadas y lotes por operador | `OperatorMetrics::next_calls`, `batches_produced` | ídem, métodos `Counted` y `CountBatch` |
| Tamaño del archivo de datos | `DiskManager::FileSize` | `src/storage/disk_manager.cpp` |

Lo que **falta** y habría que añadir:

| Métrica pedida | Estado |
|---|---|
| Distancias vectoriales calculadas | Ausente; requiere un contador en el ejecutor k-NN |
| Candidatos evaluados, nodos de índice visitados | Ausente; solo tiene sentido con un índice vectorial |
| Percentiles `p50`, `p95`, `p99`; desviación estándar | Ausentes; hoy solo se mide una latencia por sentencia, sin agregación por lotes de consultas |
| Consultas por segundo | Ausente; derivable de lo anterior |
| Exportación a CSV | Ausente; toda la salida es texto formateado para consola |
| Memoria máxima utilizada | Ausente |
| Tamaño y tiempo de construcción del índice | Ausentes; no hay índice vectorial que medir |
| `Recall@k` | Ausente; solo aplicable si se implementa búsqueda aproximada |

La distinción que pide §13 entre *acceso lógico solicitado*, *hit*, *miss* y
*lectura física* **ya está bien planteada** en el código: `hits` y `misses` se
incrementan en `BufferPoolManager::FetchPage`, y `disk_reads` solo cuando se llama
efectivamente a `DiskManager::ReadPage`. Esa parte no necesita trabajo.

---

## 6. Implementación mínima requerida

Alcance mínimo para que el artículo solicitado pueda escribirse **sin inventar
nada**. Las estimaciones de líneas incluyen pruebas.

### Fase V1 — Tipo de dato vectorial (≈380 líneas)

- Añadir `ColumnType::kVector` a `include/minidb/common/types.hpp`.
- Añadir `std::vector<float>` como tercera alternativa de `Value` en
  `include/minidb/common/value.hpp`, y decidir el comportamiento de
  `CompareValues` y `ValueLess` sobre vectores (propuesta: rechazar la comparación
  de orden con `QueryError`, porque un vector no tiene un orden natural único).
- Serialización *little-endian* explícita de `float` en
  `src/common/serialization.cpp`: `WriteF32` y `ReadF32` sobre la representación
  IEEE 754 de 32 bits, mediante `std::bit_cast<std::uint32_t>`. **No** usar
  `reinterpret_cast` de un `float*`, coherente con el criterio del resto del
  formato binario.
- Extender `Record::SerializeTo`, `Record::DeserializeFrom`, `Record::Validate` y
  `Record::SerializedSize` en `src/storage/record.cpp` con el caso vectorial:
  `2 B` de dimensión + `4·d` bytes.
- Extender la validación de `Schema` en `src/catalog/schema.cpp`: dimensión entre
  1 y una cota nueva `kMaxVectorDimension`, y prohibir que un vector sea clave
  primaria.
- Nueva constante `kMaxVectorDimension` en `include/minidb/common/constants.hpp`,
  con la demostración de que un registro sigue cabiendo en una página vacía
  (`4·d + 2 ≤ 4080`, es decir `d ≤ 1019`).
- Sintaxis SQL a añadir al *tokenizer* y al parser (**propuesta**, no
  implementada):

  ```sql
  CREATE TABLE docs (id INT PRIMARY KEY, titulo VARCHAR(80), emb VECTOR(128));
  INSERT INTO docs VALUES (1, 'ejemplo', [0.12, 0.91, ...]);
  ```

  Requiere un literal de vector entre corchetes y el reconocimiento de literales
  en punto flotante, que el *tokenizer* actual no tiene
  (`src/parser/tokenizer.cpp`, función `Tokenizer::ReadNumber`, solo lee dígitos y
  un signo menos opcional).

### Fase V2 — Métricas de distancia (≈140 líneas)

Módulo nuevo `include/minidb/vector/distance.hpp` y `src/vector/distance.cpp`:

- Distancia euclídea, y también su **cuadrado** sin la raíz. El cuadrado preserva
  el orden y evita `n` llamadas a `std::sqrt` por consulta; es la que debe usar el
  ranking, aplicando la raíz solo a los `k` resultados que se devuelven.
- Similitud coseno y distancia coseno, distinguidas explícitamente
  (`d_cos = 1 − s_cos`), con tratamiento documentado del vector nulo, cuya norma
  es cero y para el que el coseno no está definido.
- Producto punto.
- Validación de dimensiones compatibles, con `QueryError` cuando difieran.
- Sentido del orden por métrica: ascendente para distancias, descendente para
  similitudes. Es una fuente clásica de errores silenciosos y debe quedar en el
  tipo, no en el comentario.

### Fase V3 — Ejecutor de vecinos más cercanos (≈300 líneas)

- `KnnScanOperator`, operador físico con `Open`, `Next`, `Close`, que recorre la
  tabla y mantiene una **cola de prioridad acotada de tamaño `k`**
  (`std::priority_queue` con comparador invertido). Complejidad
  `O(n·d + n log k)`.
- Un segundo operador `KnnFullSortOperator`, que calcula las `n` distancias y
  ordena todas: `O(n·d + n log n)`. Es la **línea base** del experimento
  principal, y es la comparación que §4.3 admite como «ordenamiento completo
  frente a selección Top-k».
- Casos límite que deben resolverse explícitamente y probarse: `k = 0`, `k > n`,
  tabla vacía, dimensiones incompatibles, vectores duplicados, empates de
  distancia (propuesta: desempate estable por clave primaria, para que el
  resultado sea reproducible), vector nulo y valores negativos.
- Aprovechar el escaneo por lotes ya existente: `KnnScanOperator` puede tomar como
  hijo un `VectorizedScanOperator`, con lo que hereda gratis la fijación de una
  página por página en lugar de una por registro. Esto habilita una **tercera**
  comparación legítima —cálculo de distancias tupla a tupla frente a por lotes—
  que corresponde a «implementación original frente a implementación vectorizada
  optimizada» de §4.3.

### Fase V4 — Instrumentación y exportación (≈170 líneas)

- Contadores `vector_distance_calculations` y `records_examined` en el ejecutor
  k-NN, incrementados en el punto exacto del cálculo, documentado en el código.
- Agregación por lote de consultas: media, mínimo, máximo, desviación estándar,
  `p50`, `p95`, `p99` y consultas por segundo.
- Exportación a CSV desde la interfaz, sin edición manual posterior.
- Comando de comparación análogo a los existentes `.bench` y `.benchvec`.

### Fase V5 — Validación funcional (≈250 líneas)

Pruebas con casos cuyo resultado se calcula a mano, separadas de los *benchmarks*,
según §14. Con `A = [1,0]`, `B = [0,1]`, `C = [1,1]` y consulta `q = [0.9, 0.1]`,
las distancias euclídeas son verificables analíticamente y el orden esperado es
`A`, `C`, `B`. Debe comprobarse además que la selección Top-k y el ordenamiento
completo devuelven **exactamente** el mismo ranking: es la propiedad que da
validez a toda la comparación posterior.

### Fase V6 — Infraestructura experimental (≈400 líneas de Python)

Generador de vectores con semilla fija, ejecutor de *benchmarks* con
calentamiento y al menos 10 repeticiones medidas, procesamiento estadístico y
generación de gráficos, más los CSV de resultados crudos y resumidos.

### Fase V7 — Índice vectorial (opcional, ≈320 líneas)

**Solo si se quiere poder usar el término «índice vectorial» en el artículo.** Sin
esta fase, el artículo debe titularse y redactarse en términos de búsqueda
**exacta secuencial**, y no puede presentar `Recall@k` porque no habría búsqueda
aproximada que evaluar.

La opción más razonable para este sistema es **IVF-Flat**: agrupar los vectores en
`c` celdas con k-means, guardar los centroides en páginas propias con un nuevo
`PageType`, y en cada consulta explorar solo las `nprobe` celdas más cercanas al
vector de consulta. Encaja con la arquitectura porque reutiliza el patrón que ya
usa `HashIndex`: páginas físicas leídas y escritas a través del Buffer Pool, sin
estado en RAM. HNSW daría mejores resultados pero exige un grafo con
persistencia de listas de adyacencia, bastante más complejo.

### Coste total

Entre **1 340 y 1 660 líneas** según se incluya o no la fase V7, sobre un
proyecto que hoy tiene 6 399 líneas de cabeceras y fuentes. Es del orden de un
25 % de crecimiento y toca el formato binario en disco, es decir la capa más
delicada del sistema.

---

## 7. Archivos que deberían crearse o modificarse

### Crear

```text
include/minidb/vector/distance.hpp
include/minidb/execution/knn_scan_operator.hpp
include/minidb/execution/knn_full_sort_operator.hpp
src/vector/distance.cpp
src/execution/knn_operators.cpp
tests/distance_test.cpp
tests/knn_test.cpp
experimentos/scripts/generar_vectores.py
experimentos/scripts/ejecutar_benchmarks.py
experimentos/scripts/procesar_resultados.py
experimentos/scripts/generar_graficos.py
```

Y, solo con la fase V7:

```text
include/minidb/index/ivf_index.hpp
src/index/ivf_index.cpp
tests/ivf_index_test.cpp
```

### Modificar

| Archivo | Cambio |
|---|---|
| `include/minidb/common/types.hpp` | `ColumnType::kVector`; `PageType` nuevo si se añade índice |
| `include/minidb/common/constants.hpp` | `kMaxVectorDimension` con su demostración de capacidad |
| `include/minidb/common/value.hpp` | Tercera alternativa de `Value`; política de `CompareValues` y `ValueLess` |
| `include/minidb/common/serialization.hpp`, `src/common/serialization.cpp` | `WriteF32`, `ReadF32` mediante `std::bit_cast` |
| `src/storage/record.cpp` | Serializar, deserializar, validar y dimensionar el caso vectorial |
| `src/catalog/schema.cpp` | Validar la dimensión; prohibir vector como clave primaria |
| `src/catalog/catalog.cpp` | Verificar que la columna vectorial cabe en el paso de 38 B por columna de la página de catálogo |
| `include/minidb/parser/tokenizer.hpp`, `src/parser/tokenizer.cpp` | Literales en punto flotante y corchetes |
| `include/minidb/parser/statement.hpp`, `src/parser/parser.cpp` | Cláusula de consulta k-NN y literal de vector |
| `src/execution/execution_engine.cpp` | Construir el plan k-NN; elegir entre Top-k y ordenamiento completo |
| `src/main.cpp` | Presentación de vectores y distancias; comando de *benchmark* vectorial; exportación CSV |
| `CMakeLists.txt` | Fuentes y binarios de prueba nuevos |
| `.github/workflows/ci.yml` | Ejecutar las pruebas nuevas |
| `README.md`, `ARCHITECTURE.md` | Documentar el tipo, las métricas y el ejecutor |

---

## 8. Evidencia de compilación y pruebas del estado actual

Registrado para que el informe sea autocontenido. No se oculta ningún error: no
hubo ninguno.

```console
$ g++ --version | head -1
g++ (GCC) 16.1.1 20260725

$ cmake --version | head -1
cmake version 4.4.1

$ ninja --version
1.13.2

$ cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
$ cmake --build build-release -j"$(nproc)"
# Sin errores. Sin advertencias, con -Wall -Wextra -Wpedantic -Wshadow
# -Wconversion -Wsign-conversion -Wnull-dereference activos (CMakeLists.txt,
# objetivo minidb_warnings).

$ ctest --test-dir build-release -j"$(nproc)"
100% tests passed out of 264
Total Test time (real) = 0.74 sec
```

- **Estándar:** C++20 (`CMAKE_CXX_STANDARD 20`, sin extensiones).
- **Optimización:** `Release` implica `-O3 -DNDEBUG` con GCC.
- **Dependencia externa:** únicamente GoogleTest 1.17 del sistema, vía
  `find_package(GTest REQUIRED)`.
- **Pruebas fallidas:** ninguna.
- **Advertencias:** ninguna.

Las 264 pruebas cubren serialización, gestor de disco, páginas ranuradas, Buffer
Pool, montículo de tabla, índice hash, parser, modelo Volcano, operadores
bloqueantes, medición de coste y ejecución por lotes. **Ninguna** cubre similitud
vectorial, porque no hay nada que cubrir.

---

## 9. Qué sí podría escribirse hoy, sin implementar nada

Para que la decisión sea informada, conviene decir qué artículo **sí** admite el
repositorio en su estado actual. Un artículo sobre la **ejecución vectorizada de
operadores** cumpliría los cuatro criterios de la rúbrica con evidencia real:

- **Aporte distintivo:** un camino de ejecución por lotes que convive con el
  modelo Volcano tupla a tupla mediante dos adaptadores, de modo que ambos
  modelos se mezclan en un mismo plan.
- **Comparación experimental válida y ya ejecutable:** `.benchvec` mide los dos
  modelos sobre la misma consulta, los mismos datos y la misma configuración. Con
  3 000 registros se observan 60 060 llamadas a `Next()` frente a 40, y 60 360
  accesos al Buffer Pool frente a 360.
- **Segunda comparación disponible:** `.bench` mide el índice hash activado frente
  a desactivado.
- **Métricas ya instrumentadas:** latencia, lecturas físicas de disco, *hits*,
  *misses*, contadores por operador.
- **Verificabilidad adicional:** el bucle de comparación del filtro compila a
  instrucciones SIMD `pcmpgtd` con `-O3`, comprobable por desensamblado y
  verificado en integración continua.

Ese artículo sería honesto y estaría respaldado por código y por resultados. Lo
que no puede hacerse es presentarlo como búsqueda por similitud vectorial: son
preguntas de investigación distintas.

---

## 10. Recomendación

Tres caminos, en orden de coste creciente:

1. **Reorientar el artículo** a la ejecución vectorizada de operadores, que ya
   está implementada, probada y medida. Coste: solo redacción. Riesgo: ninguno.
   El artículo no podría titularse «búsqueda vectorizada por similitud».
2. **Implementar las fases V1 a V6** y escribir el artículo pedido en términos de
   **búsqueda exacta secuencial** con selección Top-k frente a ordenamiento
   completo, más la variante por lotes. Coste ≈1 340 líneas. Sin `Recall@k` y sin
   usar el término «índice vectorial», porque no habría índice.
3. **Implementar además la fase V7** (IVF-Flat) y escribir el artículo completo,
   con comparación entre búsqueda exacta y aproximada y con `Recall@k`. Coste
   ≈1 660 líneas, y modifica el formato del archivo binario.

En los tres casos, los volúmenes experimentales deben acotarse a lo que el
formato de página de 4 KiB admite razonablemente, con la justificación de la
tabla de la sección 5.2.

---

## 11. Marcadores pendientes

```text
[COMPLETAR NOMBRE DEL DOCENTE]
[COMPLETAR DATOS DEL HARDWARE]
[COMPLETAR FECHA DE EJECUCIÓN]
```

No se registran referencias bibliográficas pendientes de verificación porque este
informe no cita literatura: es una auditoría del repositorio.
