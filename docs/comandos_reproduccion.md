# Órdenes de reproducción

Todas las órdenes se ejecutan desde la raíz del repositorio. Están verificadas en el
entorno descrito al final.

---

## 1. Compilar

```bash
# Modo Release, que es el usado en los experimentos
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j"$(nproc)"

# Modo Debug, para desarrollo
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"

# Con detectores de errores de memoria y comportamiento indefinido
cmake -S . -B build-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DMINIDB_ENABLE_SANITIZERS=ON
cmake --build build-sanitize -j"$(nproc)"
```

Resultado esperado: sin errores y sin advertencias en los tres casos.

## 2. Ejecutar las pruebas

```bash
ctest --test-dir build-release --output-on-failure     # 316 casos
ctest --test-dir build-sanitize --output-on-failure    # los mismos 316
```

Solo la validación funcional del aporte:

```bash
./build-release/distance_test          # 19 casos: las métricas
./build-release/knn_test               # 33 casos: tipo, gramática y ranking

# La propiedad de la que depende toda la comparación
./build-release/knn_test --gtest_filter='*BothStrategiesReturnIdenticalResults*'
```

## 3. Generar datos

```bash
# 10 000 vectores de dimensión 64, semilla 42
python3 experimentos/scripts/generar_vectores.py --vectores 10000 --dimension 64 \
    > /tmp/carga.sql

wc -l /tmp/carga.sql        # 10 001 sentencias: el CREATE TABLE y los INSERT
```

## 4. Cargar los vectores

```bash
mkdir -p /tmp/vec
./build-release/minidb /tmp/vec/docs.db /dev/null < /tmp/carga.sql > /dev/null
ls -lh /tmp/vec/docs.db
```

## 5. Consultar de forma interactiva

```bash
./build-release/minidb /tmp/vec/docs.db
```

Dentro de la sesión:

```sql
.schema

-- Los tres vecinos más cercanos por distancia euclidiana
SELECT id, etiqueta FROM docs NEAREST emb TO [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5,
    0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5,
    0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5,
    0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5,
    0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5] LIMIT 3;

-- Búsqueda híbrida: el filtro por metadatos reduce los candidatos antes del ranking
SELECT id FROM docs WHERE id <= 100 NEAREST emb TO [...] LIMIT 3;
```

Un ejemplo corto y verificable a mano, sin necesidad de generar datos:

```bash
./build-release/minidb /tmp/vec/mano.db /dev/null <<'SQL'
CREATE TABLE docs (id INT PRIMARY KEY, titulo VARCHAR(20), emb VECTOR(2));
INSERT INTO docs VALUES (1, 'A', [1, 0]);
INSERT INTO docs VALUES (2, 'B', [0, 1]);
INSERT INTO docs VALUES (3, 'C', [1, 1]);
SELECT * FROM docs NEAREST emb TO [0.9, 0.1] LIMIT 3;
SELECT * FROM docs NEAREST emb TO [0.9, 0.1] USING COSINE LIMIT 3;
SELECT * FROM docs NEAREST emb TO [0.9, 0.1] USING DOT LIMIT 3;
SQL
```

Resultado esperado con la métrica euclidiana: `A` a `√0,02 ≈ 0,1414`, `C` a
`√0,82 ≈ 0,9055` y `B` a `√1,62 ≈ 1,2728`. Con el producto punto, que es una similitud
y por tanto ordena de forma descendente, el orden pasa a `C` (1,0), `A` (0,9), `B`
(0,1).

## 6. Cambiar la estrategia de ranking y medir

```bash
printf '.topk off\n.topk on\n.knnbench 10 30\n' \
    | ./build-release/minidb /tmp/vec/docs.db /dev/null
```

`.knnbench [k] [consultas]` ejecuta el lote con las dos estrategias e imprime media,
p50, p95, p99, consultas por segundo, distancias calculadas y métricas de buffer.

## 7. Exportar resultados crudos a CSV

```bash
./build-release/minidb /tmp/vec/docs.db /dev/null <<'SQL'
.knncsv /tmp/vec/crudos.csv 10 30
SQL
head -3 /tmp/vec/crudos.csv
```

Una fila por consulta y estrategia. El archivo se crea con encabezado si no existe y se
añade si ya existe, de modo que varias configuraciones pueden acumularse en el mismo
CSV.

## 8. Ejecutar la matriz experimental completa

```bash
# Comprobación rápida: 4 configuraciones, 10 consultas
python3 experimentos/scripts/ejecutar_benchmarks.py --rapido

# Matriz completa: 12 configuraciones, 30 consultas. Regenera los CSV.
python3 experimentos/scripts/ejecutar_benchmarks.py
```

Duración aproximada de la matriz completa: unos tres minutos de medición más el tiempo
de carga, dominado por la configuración de 100 000 vectores (unos 79 segundos).

## 9. Agregar los resultados y generar las tablas

```bash
python3 experimentos/scripts/procesar_resultados.py
```

Produce `resultados_resumen.csv`, `metricas_io.csv` y `exactitud.csv`. Termina con
código de salida distinto de cero si alguna configuración discrepa entre estrategias.

Para ver una tabla concreta del artículo:

```bash
# Tabla 3: latencia frente al número de vectores
awk -F, 'NR==1 || ($3==64 && $4==10)' \
    experimentos/resultados/resultados_resumen.csv | column -t -s,

# Tabla 7: entrada y salida
awk -F, 'NR==1 || ($3==64 && $4==10)' \
    experimentos/resultados/metricas_io.csv | column -t -s,
```

## 10. Generar las figuras

```bash
python3 experimentos/scripts/generar_graficos.py
ls experimentos/graficos/
```

Siete figuras en PNG a 150 puntos por pulgada.

## 11. Verificar las afirmaciones del artículo

```bash
# No existe ningún índice vectorial: la búsqueda es exhaustiva
grep -rniE "hnsw|ivf|lsh|quantiz|kd.?tree|ball.?tree" include src    # sin resultados

# El único índice del sistema resuelve igualdades exactas
grep -n "int32 → RecordId\|Search" include/minidb/index/hash_index.hpp | head -3

# Ambas estrategias examinan todos los registros
./build-release/knn_test \
    --gtest_filter='*BothStrategiesComputeTheSameNumberOfDistances*'

# Los invariantes arquitectónicos del sistema siguen en pie
grep -rn "DiskManager\|fstream" src/storage/table_heap.cpp src/index/ src/catalog/
```

---

## Entorno de verificación

| Elemento | Valor |
|---|---|
| Compilador | GCC 16.1.1 20260725 |
| CMake | 4.4.1 |
| Ninja | 1.13.2 |
| Estándar | C++20 |
| GoogleTest | 1.17 (paquete del sistema) |
| Python | 3.14.6 con NumPy 2.5.1 y Matplotlib 3.11.0 |
| Sistema operativo | Linux 7.1.5 (CachyOS) |
| Hardware | [COMPLETAR DATOS DEL HARDWARE] |
| Fecha de ejecución | [COMPLETAR FECHA DE EJECUCIÓN] |
