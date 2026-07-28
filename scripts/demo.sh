#!/usr/bin/env bash
#
# Demostración completa de MiniDB: crea la base desde cero, ejecuta demo.sql,
# CIERRA el proceso, lo vuelve a abrir y comprueba que los datos siguen ahí.
#
# El cierre y la reapertura son el punto: demuestran que la persistencia es real
# y no un estado que vive en RAM mientras el programa está en marcha.

set -euo pipefail

cd "$(dirname "$0")/.."

DB="data/minidb.db"
BINARY="build/minidb"

echo "=============================================================="
echo " 1. Compilación"
echo "=============================================================="
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build build -j"$(nproc)"
echo

echo "=============================================================="
echo " 2. Base de datos limpia"
echo "=============================================================="
rm -f "$DB"
echo "Eliminado $DB (si existía). El archivo se creará desde cero."
echo

echo "=============================================================="
echo " 3. Ejecución de examples/demo.sql"
echo "=============================================================="
"$BINARY" "$DB" < examples/demo.sql
echo

echo "=============================================================="
echo " 4. El proceso ha terminado. Tamaño del archivo en disco:"
echo "=============================================================="
ls -l "$DB"
SIZE_AFTER_DEMO=$(stat -c %s "$DB")
echo

echo "=============================================================="
echo " 5. REAPERTURA: proceso nuevo, misma base de datos"
echo "=============================================================="
"$BINARY" "$DB" <<'SQL'
SELECT * FROM students;
SELECT * FROM students WHERE id = 1;
.schema
SQL
echo

echo "=============================================================="
echo " 6. Reutilización de espacio: borrar todo y reinsertar"
echo "=============================================================="
echo "El archivo NO debe crecer: el espacio liberado se reutiliza."
echo "Tamaño antes: $SIZE_AFTER_DEMO bytes"
"$BINARY" "$DB" <<'SQL' > /dev/null
DELETE FROM students;
INSERT INTO students VALUES (1, 'Ana', 21, 'Ciencia de la Computación');
INSERT INTO students VALUES (9, 'María', 19, 'Ingeniería de Software');
SQL
SIZE_AFTER_CYCLE=$(stat -c %s "$DB")
echo "Tamaño después: $SIZE_AFTER_CYCLE bytes"

if [ "$SIZE_AFTER_DEMO" -eq "$SIZE_AFTER_CYCLE" ]; then
    echo "OK: el archivo no creció."
else
    echo "AVISO: el archivo cambió de tamaño ($SIZE_AFTER_DEMO -> $SIZE_AFTER_CYCLE)."
    exit 1
fi
echo

echo "=============================================================="
echo " Demostración completada"
echo "=============================================================="
