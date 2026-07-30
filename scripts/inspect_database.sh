#!/usr/bin/env bash
#
# Evidencia en disco de los archivos que forma el sistema: existencia, tamaño y
# contenido binario. Produce las capturas que pide la entrega.

set -uo pipefail

cd "$(dirname "$0")/.."

DB="${1:-data/minidb.db}"
CONF="minidb.conf"

if [ ! -f "$DB" ]; then
    echo "No existe $DB. Ejecute antes: bash scripts/demo.sh"
    exit 1
fi

echo "=============================================================="
echo " Archivos del sistema"
echo "=============================================================="
ls -lh "$DB" "$CONF"
echo

echo "=============================================================="
echo " Archivo binario de la base de datos: $DB"
echo "=============================================================="
stat "$DB"
echo
echo "Tipo detectado por 'file':"
file "$DB"
echo

SIZE=$(stat -c %s "$DB")
PAGES=$((SIZE / 4096))
echo "Tamaño:            $SIZE bytes"
echo "Tamaño de página:  4096 bytes"
echo "Páginas:           $PAGES"
echo "Resto (debe ser 0): $((SIZE % 4096))"
echo

echo "=============================================================="
echo " Página 0: cabecera del archivo"
echo "=============================================================="
echo "Se esperan los bytes 'MIND', la versión, el tamaño de página (0x1000),"
echo "el número de páginas, la cabeza de la lista libre y la página de catálogo."
xxd -l 32 "$DB"
echo

echo "=============================================================="
echo " Página 1: catálogo (esquema de la tabla)"
echo "=============================================================="
echo "Se esperan los nombres de la tabla y de sus columnas en texto plano."
xxd -s 4096 -l 160 "$DB"
echo

echo "=============================================================="
echo " Página 2: cabecera del índice hash"
echo "=============================================================="
echo "Tipo de página (04), número de buckets, capacidad y los page id de cada bucket."
xxd -s 8192 -l 80 "$DB"
echo

echo "=============================================================="
echo " Archivo de configuración: $CONF"
echo "=============================================================="
stat -c '%n: %s bytes' "$CONF"
echo
cat "$CONF"
echo

echo "=============================================================="
echo " Archivos temporales"
echo "=============================================================="
echo "El sistema no genera archivos temporales. ORDER BY y GROUP BY sí son"
echo "operadores bloqueantes (materializan su entrada antes de emitir la primera"
echo "fila), pero lo hacen en RAM, acotados por el tamaño de la tabla. Una"
echo "ordenación externa por mezcla, que volcaría los tramos ordenados a disco,"
echo "sería el paso siguiente y no está implementada."
echo
echo "Los archivos del sistema son, por tanto, el binario de datos y el archivo"
echo "de configuración. El script scripts/benchmark.sh crea además data/bench.db,"
echo "una segunda base de datos independiente para la evaluación de rendimiento."
if [ -f data/bench.db ]; then
    echo
    echo "Base de datos de evaluación presente:"
    ls -lh data/bench.db
fi
