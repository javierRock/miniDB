# Evidencias de código de la búsqueda por similitud vectorial

Complemento del anexo A de `articulo_busqueda_vectorizada.md`. Aquí se recogen los
fragmentos concretos y los números de partida de cada afirmación, para que cualquier
revisor pueda ir del artículo al código sin buscar.

Commit evaluado: el que introduce la característica, `feat: add VECTOR type and exact
k-NN similarity search`. Compilación: GCC 16.1.1, C++20, `Release` (`-O3 -DNDEBUG`),
sin advertencias.

---

## 1. Archivos que componen el aporte

### Nuevos

| Archivo | Líneas | Contenido |
|---|---:|---|
| `include/minidb/vector/distance.hpp` | 71 | Interfaz de las métricas |
| `src/vector/distance.cpp` | 125 | Implementación de las métricas |
| `include/minidb/execution/knn_scan_operator.hpp` | 85 | Operador de selección Top-k |
| `include/minidb/execution/knn_full_sort_operator.hpp` | 42 | Operador de línea base |
| `src/execution/knn_operators.cpp` | 165 | Implementación de ambos |
| `tests/distance_test.cpp` | 193 | 19 casos sobre las métricas |
| `tests/knn_test.cpp` | 542 | 33 casos sobre el tipo, la gramática y el ranking |
| `experimentos/scripts/*.py` | 4 guiones | Generación, ejecución, agregación y figuras |

### Modificados

| Archivo | Cambio |
|---|---|
| `include/minidb/common/types.hpp` | `ColumnType::kVector`; `DistanceMetric` |
| `include/minidb/common/constants.hpp` | `kMaxRecordSize`, `kMaxVectorDimension` |
| `include/minidb/common/value.hpp` | `Vector`; `Value` con dos alternativas más |
| `src/common/value.cpp` | Representación textual y política de comparación |
| `include/minidb/common/serialization.hpp`, `src/common/serialization.cpp` | `WriteF32`, `ReadF32`, `VectorSize` |
| `src/storage/record.cpp` | Caso vectorial en validación, tamaño y (de)serialización |
| `src/catalog/schema.cpp` | Validación de dimensión y del registro más ancho |
| `src/catalog/catalog.cpp` | Rechazo de tipos de columna desconocidos al recargar |
| `include/minidb/storage/table_page.hpp` | `kMaxRecordSize` con `static_assert` de coherencia |
| `include/minidb/parser/tokenizer.hpp`, `src/parser/tokenizer.cpp` | Literales decimales, corchetes y cinco palabras clave |
| `include/minidb/parser/statement.hpp`, `src/parser/parser.cpp` | `NearestClause` y su gramática |
| `include/minidb/execution/physical_operator.hpp` | Contadores de distancias y de candidatos |
| `include/minidb/execution/execution_engine.hpp`, `src/execution/execution_engine.cpp` | Plan k-NN y conmutador de estrategia |
| `include/minidb/database/database.hpp`, `src/database/database.cpp` | Paso del conmutador |
| `src/main.cpp` | Órdenes `.topk`, `.knnbench` y `.knncsv`; presentación de vectores |
| `CMakeLists.txt` | Fuentes y binarios de prueba nuevos |

---

## 2. Representación y persistencia

**El tipo de columna.**

```cpp
enum class ColumnType : std::uint8_t {
    kInteger = 1,
    kVarchar = 2,
    /// Fixed-dimension array of 32-bit floats, for similarity search. The
    /// declared dimension is stored in Column::max_length, as VARCHAR stores its
    /// byte limit there.
    kVector = 3,
};
```

Fuente: `include/minidb/common/types.hpp`, enumerado `ColumnType`

**El valor.** La cuarta alternativa, `float` escalar, no corresponde a ningún tipo de
columna: existe únicamente para que la distancia calculada circule por el plan.

```cpp
using Vector = std::vector<float>;
using Value = std::variant<std::int32_t, std::string, Vector, float>;
```

Fuente: `include/minidb/common/value.hpp`

**Escritura en disco.**

```cpp
const Vector& vector = std::get<Vector>(values_[i]);
serialization::WriteU16(destination, offset, static_cast<std::uint16_t>(vector.size()));
offset += sizeof(std::uint16_t);
for (float component : vector) {
    serialization::WriteF32(destination, offset, component);
    offset += sizeof(float);
}
```

Fuente: `src/storage/record.cpp`, función `Record::SerializeTo`

**Lectura, con detección de longitud corrupta.**

```cpp
const std::uint16_t dimension = serialization::ReadU16(source, offset);
offset += sizeof(std::uint16_t);
if (dimension != column.max_length) {
    throw StorageError("La columna '" + column.name + "' es VECTOR(" + ...);
}
```

Fuente: `src/storage/record.cpp`, función `Record::DeserializeFrom`

**El invariante de capacidad restaurado.**

```cpp
const std::size_t widest_record = MaxSerializedSize();
if (widest_record > kMaxRecordSize) {
    throw QueryError("El registro más ancho posible de esta tabla ocuparía " + ...);
}
```

Fuente: `src/catalog/schema.cpp`, constructor `Schema::Schema`

Comprobación: `4 + 2 + 4·1000 = 4006 ≤ 4080` para una tabla de una clave y un vector de
1000 dimensiones; `4 + 2·(2 + 4·1000) = 8008 > 4080` para dos vectores, que se rechaza.

---

## 3. Métricas

Las cinco funciones públicas son `SquaredEuclideanDistance`, `EuclideanDistance`,
`DotProduct`, `CosineSimilarity` y `CosineDistance`, más `RankingScore`,
`ReportedDistance`, `MetricName` y `LowerIsCloser`.

**Acumulación en doble precisión.**

```cpp
double total = 0.0;
for (std::size_t i = 0; i < left.size(); ++i) {
    const double difference = static_cast<double>(left[i]) - static_cast<double>(right[i]);
    total += difference * difference;
}
return static_cast<float>(total);
```

Fuente: `src/vector/distance.cpp`, función `vector_metrics::SquaredEuclideanDistance`

**Vector nulo.**

```cpp
if (left_norm == 0.0 || right_norm == 0.0) {
    return 0.0F;
}
```

Fuente: `src/vector/distance.cpp`, función `vector_metrics::CosineSimilarity`

Verificación de que no produce `NaN`: `tests/distance_test.cpp`, caso
`DistanceTest.CosineWithTheZeroVectorIsDefinedAsZeroSimilarity`.

---

## 4. Ranking

**Desempate reproducible.**

```cpp
if (left.score != right.score) {
    return left.score < right.score;
}
return left.key < right.key;
```

Fuente: `src/execution/knn_operators.cpp`, función `KnnScanOperator::Closer`

**Puntuación compartida por ambas estrategias.** Es el punto que garantiza que la
aritmética medida sea idéntica:

```cpp
KnnScanOperator::Candidate KnnScanOperator::Score(Record record) { ... }
```

Fuente: `src/execution/knn_operators.cpp`, función `KnnScanOperator::Score`; la usan
`KnnScanOperator::Open` y `KnnFullSortOperator::Open`.

**Construcción del plan.**

```cpp
std::unique_ptr<PhysicalOperator> knn =
    topk_enabled_
        ? std::make_unique<KnnScanOperator>(std::move(plan), schema, *select.nearest)
        : std::make_unique<KnnFullSortOperator>(std::move(plan), schema, *select.nearest);
```

Fuente: `src/execution/execution_engine.cpp`, función `ExecutionEngine::BuildPlan`

---

## 5. Instrumentación

| Contador | Dónde se incrementa | Significado exacto |
|---|---|---|
| `distance_calculations` | `KnnScanOperator::Score` | Una por cada cálculo de puntuación de ranking |
| `candidates_admitted` | `KnnScanOperator::Open`, `KnnFullSortOperator::Open` | Candidatos retenidos por la estructura de ranking |
| `rows_produced`, `next_calls` | `PhysicalOperator::Counted` | Filas entregadas y llamadas recibidas |
| `hits`, `misses` | `BufferPoolManager::FetchPage` | Acceso **lógico** resuelto en RAM o no |
| `disk_reads` | Junto a `DiskManager::ReadPage` | Lectura **física** de una página |
| `disk_writes` | Camino de desalojo y de sincronización | Escritura física |
| `elapsed_ms` | `Database::Execute` | Reloj monótono, incluye el análisis sintáctico |

La distinción entre acceso lógico, acierto, fallo y lectura física ya existía en el
sistema y no se modificó: `hits + misses` cuenta peticiones de página, y `disk_reads`
solo las que llegaron al archivo.

---

## 6. Cómo comprobar cada afirmación

```bash
# El tipo existe y solo hay tres
grep -n "kVector" include/minidb/common/types.hpp

# No hay ningún índice vectorial: el único índice es de igualdad exacta
grep -rn "class HashIndex" -A 6 include/minidb/index/hash_index.hpp
grep -rniE "hnsw|ivf|lsh|quantiz|proximity graph" include src   # sin resultados

# Ambas estrategias examinan todos los registros
./build-release/knn_test --gtest_filter='*BothStrategiesComputeTheSameNumberOfDistances*'

# Las dos devuelven exactamente lo mismo
./build-release/knn_test --gtest_filter='*BothStrategiesReturnIdenticalResults*'

# Las métricas coinciden con valores calculados a mano
./build-release/distance_test
```
