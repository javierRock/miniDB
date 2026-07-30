#!/usr/bin/env bash
#
# Demostración de la búsqueda por similitud vectorial.
#
# Usa una base de datos propia porque el sistema admite una sola tabla. El
# ejemplo pequeño es verificable a mano; el grande muestra la evaluación.

set -euo pipefail

cd "$(dirname "$0")/.."

DB="data/vectorial.db"
BINARY="build/minidb"
ROWS="${VEC_ROWS:-2000}"
DIM="${VEC_DIM:-32}"

echo "=============================================================="
echo " 1. Compilación"
echo "=============================================================="
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build build -j"$(nproc)"
echo

echo "=============================================================="
echo " 2. Ejemplo verificable a mano"
echo "=============================================================="
echo "A=[1,0]  B=[0,1]  C=[1,1]   consulta q=[0.9, 0.1]"
echo "Distancias euclidianas esperadas: A=sqrt(0.02)=0.1414,"
echo "C=sqrt(0.82)=0.9055, B=sqrt(1.62)=1.2728  ->  orden A, C, B"
echo
rm -f data/mano.db
"$BINARY" data/mano.db /dev/null <<'SQL'
CREATE TABLE docs (id INT PRIMARY KEY, titulo VARCHAR(20), emb VECTOR(2));
INSERT INTO docs VALUES (1, 'A', [1, 0]);
INSERT INTO docs VALUES (2, 'B', [0, 1]);
INSERT INTO docs VALUES (3, 'C', [1, 1]);
SELECT * FROM docs NEAREST emb TO [0.9, 0.1] LIMIT 3;
SELECT * FROM docs NEAREST emb TO [0.9, 0.1] USING COSINE LIMIT 3;
SELECT * FROM docs NEAREST emb TO [0.9, 0.1] USING DOT LIMIT 3;
.schema
SQL
echo

echo "=============================================================="
echo " 3. Carga de $ROWS vectores de dimensión $DIM"
echo "=============================================================="
mkdir -p data
rm -f "$DB"
python3 experimentos/scripts/generar_vectores.py --vectores "$ROWS" --dimension "$DIM" \
    | "$BINARY" "$DB" /dev/null >/dev/null
ls -lh "$DB"
echo

echo "=============================================================="
echo " 4. Las dos estrategias de ranking sobre las mismas consultas"
echo "=============================================================="
"$BINARY" "$DB" /dev/null <<'SQL'
.knnbench 10 20
SQL
echo

echo "=============================================================="
echo " 5. Búsqueda híbrida: el filtro reduce las distancias calculadas"
echo "=============================================================="
echo "La misma consulta sin filtro y con WHERE id <= 100. Compare el contador"
echo "de distancias del plan: el filtro actúa antes de la aritmética vectorial."
echo
QUERY=$(python3 -c "print(','.join('0.5' for _ in range($DIM)))")
"$BINARY" "$DB" /dev/null <<SQL
SELECT id FROM docs NEAREST emb TO [$QUERY] LIMIT 3;
SELECT id FROM docs WHERE id <= 100 NEAREST emb TO [$QUERY] LIMIT 3;
SQL
echo

echo "=============================================================="
echo " Demostración vectorial completada"
echo "=============================================================="
