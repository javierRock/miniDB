#!/usr/bin/env python3
"""Genera las figuras del artículo a partir de resultados_resumen.csv.

Cada figura corresponde a una pregunta de investigación. No se dibuja ninguna
curva que no provenga de una fila del CSV.
"""

import csv
import pathlib
import sys

import matplotlib

matplotlib.use("Agg")  # sin servidor de ventanas
import matplotlib.pyplot as plt  # noqa: E402

RAIZ = pathlib.Path(__file__).resolve().parents[2]
RESULTADOS = RAIZ / "experimentos" / "resultados"
GRAFICOS = RAIZ / "experimentos" / "graficos"

ETIQUETAS = {"topk": "Top-k acotado, O(n log k)", "fullsort": "Orden completo, O(n log n)"}
COLORES = {"topk": "#1f77b4", "fullsort": "#d62728"}
MARCAS = {"topk": "o", "fullsort": "s"}

DIMENSION_BASE = 64
K_BASE = 10


def leer(nombre: str) -> list[dict]:
    ruta = RESULTADOS / nombre
    if not ruta.exists():
        raise SystemExit(f"No existe {ruta}. Ejecute antes procesar_resultados.py")
    with ruta.open(newline="") as fichero:
        return list(csv.DictReader(fichero))


def serie(filas: list[dict], estrategia: str, eje: str, fijos: dict) -> tuple[list, list]:
    puntos = []
    for fila in filas:
        if fila["estrategia"] != estrategia:
            continue
        if any(int(fila[clave]) != valor for clave, valor in fijos.items()):
            continue
        puntos.append((int(fila[eje]), float(fila["media_ms"])))
    puntos.sort()
    return [x for x, _ in puntos], [y for _, y in puntos]


def figura_linea(filas, eje, fijos, titulo, etiqueta_x, nombre, log=False):
    plt.figure(figsize=(6.4, 4.0))
    dibujado = False
    for estrategia in ("topk", "fullsort"):
        x, y = serie(filas, estrategia, eje, fijos)
        if not x:
            continue
        dibujado = True
        plt.plot(x, y, marker=MARCAS[estrategia], color=COLORES[estrategia],
                 label=ETIQUETAS[estrategia])
    if not dibujado:
        plt.close()
        print(f"  (sin datos para {nombre})")
        return

    plt.title(titulo)
    plt.xlabel(etiqueta_x)
    plt.ylabel("Latencia media por consulta (ms)")
    if log:
        plt.xscale("log")
        plt.yscale("log")
    plt.grid(True, which="both", alpha=0.3)
    plt.legend()
    plt.tight_layout()
    destino = GRAFICOS / nombre
    plt.savefig(destino, dpi=150)
    plt.close()
    print(f"  {destino.name}")


def figura_distancias(filas):
    """Distancias calculadas frente a n: la comprobación de que el coste
    aritmético es lineal e idéntico en las dos estrategias."""
    plt.figure(figsize=(6.4, 4.0))
    dibujado = False
    for estrategia in ("topk", "fullsort"):
        puntos = []
        for fila in filas:
            if fila["estrategia"] != estrategia:
                continue
            if int(fila["dimension"]) != DIMENSION_BASE or int(fila["k"]) != K_BASE:
                continue
            puntos.append((int(fila["vectores"]), float(fila["distancias_por_consulta"])))
        puntos.sort()
        if not puntos:
            continue
        dibujado = True
        plt.plot([x for x, _ in puntos], [y for _, y in puntos],
                 marker=MARCAS[estrategia], color=COLORES[estrategia],
                 label=ETIQUETAS[estrategia], linestyle="-" if estrategia == "topk" else "--")
    if not dibujado:
        plt.close()
        return

    plt.title("Distancias calculadas por consulta")
    plt.xlabel("Vectores almacenados (n)")
    plt.ylabel("Distancias por consulta")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(GRAFICOS / "distancias_vs_vectores.png", dpi=150)
    plt.close()
    print("  distancias_vs_vectores.png")


def figura_io():
    """Aciertos y fallos del buffer frente a n."""
    filas = leer("metricas_io.csv")
    puntos = []
    for fila in filas:
        if fila["estrategia"] != "topk":
            continue
        if int(fila["dimension"]) != DIMENSION_BASE or int(fila["k"]) != K_BASE:
            continue
        puntos.append(
            (
                int(fila["vectores"]),
                float(fila["buffer_hits_por_consulta"]),
                float(fila["buffer_misses_por_consulta"]),
                float(fila["page_reads_por_consulta"]),
            )
        )
    puntos.sort()
    if not puntos:
        return

    x = [p[0] for p in puntos]
    plt.figure(figsize=(6.4, 4.0))
    plt.plot(x, [p[1] for p in puntos], marker="o", label="Aciertos de buffer")
    plt.plot(x, [p[2] for p in puntos], marker="s", label="Fallos de buffer")
    plt.plot(x, [p[3] for p in puntos], marker="^", linestyle="--",
             label="Lecturas físicas de página")
    plt.title("Comportamiento del Buffer Pool por consulta")
    plt.xlabel("Vectores almacenados (n)")
    plt.ylabel("Operaciones por consulta")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(GRAFICOS / "buffer_vs_vectores.png", dpi=150)
    plt.close()
    print("  buffer_vs_vectores.png")


def figura_tamano():
    ruta = RESULTADOS / "carga.csv"
    if not ruta.exists():
        return
    with ruta.open(newline="") as fichero:
        filas = list(csv.DictReader(fichero))

    puntos = sorted(
        (int(f["vectores"]), int(f["bytes_archivo"]) / (1024 * 1024))
        for f in filas
        if int(f["dimension"]) == DIMENSION_BASE
    )
    if not puntos:
        return

    plt.figure(figsize=(6.4, 4.0))
    plt.plot([x for x, _ in puntos], [y for _, y in puntos], marker="o", color="#2ca02c")
    plt.title(f"Tamaño del archivo de datos (d = {DIMENSION_BASE})")
    plt.xlabel("Vectores almacenados (n)")
    plt.ylabel("Tamaño en disco (MiB)")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(GRAFICOS / "tamano_vs_vectores.png", dpi=150)
    plt.close()
    print("  tamano_vs_vectores.png")


def main() -> int:
    GRAFICOS.mkdir(parents=True, exist_ok=True)
    filas = leer("resultados_resumen.csv")
    print("Figuras generadas:")

    figura_linea(
        filas,
        "vectores",
        {"dimension": DIMENSION_BASE, "k": K_BASE},
        f"Latencia frente al número de vectores (d = {DIMENSION_BASE}, k = {K_BASE})",
        "Vectores almacenados (n)",
        "latencia_vs_vectores.png",
    )
    figura_linea(
        filas,
        "vectores",
        {"dimension": DIMENSION_BASE, "k": K_BASE},
        f"Latencia frente al número de vectores, escala logarítmica (d = {DIMENSION_BASE})",
        "Vectores almacenados (n)",
        "latencia_vs_vectores_log.png",
        log=True,
    )

    dimensiones = sorted({int(f["vectores"]) for f in filas if int(f["dimension"]) != DIMENSION_BASE})
    base_dimension = dimensiones[0] if dimensiones else 10000
    figura_linea(
        filas,
        "dimension",
        {"vectores": base_dimension, "k": K_BASE},
        f"Latencia frente a la dimensionalidad (n = {base_dimension}, k = {K_BASE})",
        "Dimensión del vector (d)",
        "latencia_vs_dimension.png",
    )
    figura_linea(
        filas,
        "k",
        {"vectores": base_dimension, "dimension": DIMENSION_BASE},
        f"Latencia frente a k (n = {base_dimension}, d = {DIMENSION_BASE})",
        "Vecinos solicitados (k)",
        "latencia_vs_k.png",
    )

    figura_distancias(filas)
    figura_io()
    figura_tamano()
    return 0


if __name__ == "__main__":
    sys.exit(main())
