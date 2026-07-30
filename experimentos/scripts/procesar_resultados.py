#!/usr/bin/env python3
"""Agrega los resultados crudos en las tablas que cita el artículo.

Lee `resultados/resultados_crudos.csv` —una fila por consulta y estrategia— y
produce:

  resultados_resumen.csv   estadísticas de latencia por configuración
  metricas_io.csv          páginas leídas, aciertos y fallos del buffer
  exactitud.csv            comprobación de que las dos estrategias coinciden

Los percentiles se calculan por rango más cercano sobre la muestra ordenada, sin
interpolar: con 30 observaciones interpolar sugeriría una precisión que la muestra
no tiene.
"""

import csv
import math
import pathlib
import statistics
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]
RESULTADOS = RAIZ / "experimentos" / "resultados"


def percentil(ordenada: list[float], fraccion: float) -> float:
    if not ordenada:
        return 0.0
    indice = min(int(fraccion * len(ordenada)), len(ordenada) - 1)
    return ordenada[indice]


def leer_crudos(ruta: pathlib.Path) -> list[dict]:
    if not ruta.exists():
        raise SystemExit(f"No existe {ruta}. Ejecute antes ejecutar_benchmarks.py")
    with ruta.open(newline="") as fichero:
        return list(csv.DictReader(fichero))


def agrupar(filas: list[dict]) -> dict:
    grupos: dict = {}
    for fila in filas:
        clave = (
            fila["estrategia"],
            int(fila["vectores"]),
            int(fila["dimension"]),
            int(fila["k"]),
        )
        grupos.setdefault(clave, []).append(fila)
    return grupos


def main() -> int:
    filas = leer_crudos(RESULTADOS / "resultados_crudos.csv")
    grupos = agrupar(filas)

    resumen = RESULTADOS / "resultados_resumen.csv"
    with resumen.open("w", newline="") as fichero:
        escritor = csv.writer(fichero)
        escritor.writerow(
            [
                "estrategia",
                "vectores",
                "dimension",
                "k",
                "consultas",
                "media_ms",
                "minimo_ms",
                "maximo_ms",
                "desviacion_ms",
                "p50_ms",
                "p95_ms",
                "p99_ms",
                "consultas_por_segundo",
                "distancias_por_consulta",
            ]
        )
        for clave in sorted(grupos):
            estrategia, vectores, dimension, k = clave
            muestras = grupos[clave]
            latencias = sorted(float(f["latencia_ms"]) for f in muestras)
            distancias = [int(f["distancias"]) for f in muestras]

            media = statistics.fmean(latencias)
            escritor.writerow(
                [
                    estrategia,
                    vectores,
                    dimension,
                    k,
                    len(latencias),
                    f"{media:.4f}",
                    f"{latencias[0]:.4f}",
                    f"{latencias[-1]:.4f}",
                    f"{statistics.pstdev(latencias):.4f}",
                    f"{percentil(latencias, 0.50):.4f}",
                    f"{percentil(latencias, 0.95):.4f}",
                    f"{percentil(latencias, 0.99):.4f}",
                    f"{1000.0 / media:.1f}" if media > 0 else "0",
                    f"{statistics.fmean(distancias):.1f}",
                ]
            )

    io = RESULTADOS / "metricas_io.csv"
    with io.open("w", newline="") as fichero:
        escritor = csv.writer(fichero)
        escritor.writerow(
            [
                "estrategia",
                "vectores",
                "dimension",
                "k",
                "page_reads_por_consulta",
                "buffer_hits_por_consulta",
                "buffer_misses_por_consulta",
                "tasa_aciertos",
                "registros_examinados_por_consulta",
            ]
        )
        for clave in sorted(grupos):
            estrategia, vectores, dimension, k = clave
            muestras = grupos[clave]
            lecturas = statistics.fmean(int(f["page_reads"]) for f in muestras)
            aciertos = statistics.fmean(int(f["buffer_hits"]) for f in muestras)
            fallos = statistics.fmean(int(f["buffer_misses"]) for f in muestras)
            examinados = statistics.fmean(int(f["registros_examinados"]) for f in muestras)
            total = aciertos + fallos
            escritor.writerow(
                [
                    estrategia,
                    vectores,
                    dimension,
                    k,
                    f"{lecturas:.1f}",
                    f"{aciertos:.1f}",
                    f"{fallos:.1f}",
                    f"{aciertos / total:.4f}" if total > 0 else "0",
                    f"{examinados:.1f}",
                ]
            )

    # Exactitud. La búsqueda es exacta en las dos estrategias, así que lo
    # comprobable aquí es que devuelven el mismo número de filas y calculan el
    # mismo número de distancias sobre las mismas consultas. La igualdad de los
    # vecinos concretos se comprueba en tests/knn_test.cpp, donde se pueden
    # comparar registro a registro.
    exactitud = RESULTADOS / "exactitud.csv"
    with exactitud.open("w", newline="") as fichero:
        escritor = csv.writer(fichero)
        escritor.writerow(
            [
                "vectores",
                "dimension",
                "k",
                "filas_topk",
                "filas_orden_completo",
                "distancias_topk",
                "distancias_orden_completo",
                "coinciden",
            ]
        )
        configuraciones = sorted(
            {(v, d, k) for (_, v, d, k) in grupos}
        )
        for vectores, dimension, k in configuraciones:
            topk = grupos.get(("topk", vectores, dimension, k), [])
            full = grupos.get(("fullsort", vectores, dimension, k), [])
            if not topk or not full:
                continue
            filas_topk = sum(int(f["filas"]) for f in topk)
            filas_full = sum(int(f["filas"]) for f in full)
            dist_topk = sum(int(f["distancias"]) for f in topk)
            dist_full = sum(int(f["distancias"]) for f in full)
            escritor.writerow(
                [
                    vectores,
                    dimension,
                    k,
                    filas_topk,
                    filas_full,
                    dist_topk,
                    dist_full,
                    "sí" if filas_topk == filas_full and dist_topk == dist_full else "NO",
                ]
            )

    print(f"Escritos:\n  {resumen}\n  {io}\n  {exactitud}")

    # Aviso visible si alguna configuración no coincide: sería un error de
    # corrección, no de rendimiento, y no debe pasar desapercibido.
    with exactitud.open(newline="") as fichero:
        discrepancias = [f for f in csv.DictReader(fichero) if f["coinciden"] == "NO"]
    if discrepancias:
        print(f"\nAVISO: {len(discrepancias)} configuraciones NO coinciden entre estrategias.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
