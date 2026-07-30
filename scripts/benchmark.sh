#!/usr/bin/env bash
#
# Evaluación de rendimiento: mide el coste de las mismas búsquedas por clave
# primaria resueltas con índice hash y con escaneo secuencial.
#
# Usa su propia base de datos (data/bench.db) para no alterar la de la demo, y
# la reconstruye en cada ejecución para que la medición sea reproducible.
#
# Variables de entorno:
#   BENCH_ROWS     filas a insertar        (por defecto 3000)
#   BENCH_QUERIES  consultas por ronda     (por defecto 100)

set -euo pipefail

cd "$(dirname "$0")/.."

DB="data/bench.db"
BINARY="build/minidb"
ROWS="${BENCH_ROWS:-3000}"
QUERIES="${BENCH_QUERIES:-100}"

echo "=============================================================="
echo " 1. Compilación"
echo "=============================================================="
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build build -j"$(nproc)"
echo

echo "=============================================================="
echo " 2. Carga de $ROWS filas en $DB"
echo "=============================================================="
mkdir -p data
rm -f "$DB"

# El script SQL se genera aquí para que el volumen de datos sea un parámetro y
# no un archivo de 3000 líneas en el repositorio.
{
    echo "CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR(50), age INT, career VARCHAR(50));"
    for ((i = 1; i <= ROWS; i++)); do
        echo "INSERT INTO students VALUES ($i, 'estudiante$i', $((18 + i % 30)), 'carrera$((i % 5))');"
    done
} | "$BINARY" "$DB" >/dev/null

echo "Cargadas $ROWS filas."
ls -lh "$DB"
echo

echo "=============================================================="
echo " 3. Comparación con y sin índice ($QUERIES consultas por ronda)"
echo "=============================================================="
"$BINARY" "$DB" <<SQL
.bench $QUERIES
SQL
echo

echo "=============================================================="
echo " 4. Plan y coste de una consulta de cada tipo"
echo "=============================================================="
# Una clave que existe seguro, cualquiera que sea el número de filas.
KEY=$((ROWS / 2))
"$BINARY" "$DB" <<SQL
SELECT * FROM students WHERE id = $KEY;
.indice off
SELECT * FROM students WHERE id = $KEY;
.indice on
SELECT career, COUNT(*) FROM students GROUP BY career;
SQL
echo

echo "=============================================================="
echo " 5. Estado del Buffer Pool tras la evaluación"
echo "=============================================================="
"$BINARY" "$DB" <<'SQL'
.buffer
.pages
SQL

echo
echo "=============================================================="
echo " Evaluación completada"
echo "=============================================================="
