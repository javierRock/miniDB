#!/usr/bin/env python3
"""Ejecuta la matriz experimental completa y guarda los resultados crudos.

Para cada combinación de (número de vectores, dimensión, k):

  1. Crea una base de datos nueva y la carga con los vectores generados.
  2. Registra el tiempo de carga y el tamaño del archivo en disco.
  3. Invoca `.knncsv`, que ejecuta el mismo lote de consultas con las dos
     estrategias de ranking y añade una fila por consulta al CSV.

Las consultas las genera el propio binario a partir de una semilla fija, de modo
que las dos estrategias ven consultas idénticas byte a byte y el experimento no
depende de este script para su reproducibilidad.

Uso:
    python3 ejecutar_benchmarks.py --rapido
    python3 ejecutar_benchmarks.py            # matriz completa
"""

import argparse
import csv
import os
import pathlib
import shutil
import subprocess
import sys
import time

RAIZ = pathlib.Path(__file__).resolve().parents[2]
BINARIO = RAIZ / "build-release" / "minidb"
GENERADOR = pathlib.Path(__file__).resolve().parent / "generar_vectores.py"
RESULTADOS = RAIZ / "experimentos" / "resultados"
DATOS = RAIZ / "experimentos" / "datos" / "datos_generados"

# Volúmenes acotados a lo que el formato de página de 4 KiB admite con holgura.
# Con d = 64 un vector ocupa 258 bytes y caben 15 por página, de modo que 100 000
# vectores son unas 6 700 páginas (26 MiB). Ver la sección de amenazas a la
# validez del artículo.
VECTORES = [1000, 10000, 50000, 100000]
DIMENSIONES = [16, 32, 64, 128, 256]
KS = [1, 5, 10, 20, 50]

VECTORES_BASE = 10000
DIMENSION_BASE = 64
K_BASE = 10
CONSULTAS = 30


def ejecutar(comandos: str, base: pathlib.Path) -> str:
    """Envía órdenes al Mini-SGBD por la entrada estándar."""
    resultado = subprocess.run(
        [str(BINARIO), str(base), "/dev/null"],
        input=comandos,
        capture_output=True,
        text=True,
        check=False,
    )
    if resultado.returncode != 0:
        print(resultado.stdout[-2000:], file=sys.stderr)
        print(resultado.stderr[-2000:], file=sys.stderr)
        raise SystemExit(f"El Mini-SGBD terminó con código {resultado.returncode}")
    return resultado.stdout


def cargar(vectores: int, dimension: int) -> tuple[pathlib.Path, float, int]:
    """Crea y puebla una base de datos. Devuelve ruta, segundos y bytes."""
    DATOS.mkdir(parents=True, exist_ok=True)
    guion = DATOS / f"carga_{vectores}_{dimension}.sql"

    if not guion.exists():
        with guion.open("w") as destino:
            subprocess.run(
                [
                    sys.executable,
                    str(GENERADOR),
                    "--vectores",
                    str(vectores),
                    "--dimension",
                    str(dimension),
                ],
                stdout=destino,
                check=True,
            )

    base = DATOS / f"docs_{vectores}_{dimension}.db"
    base.unlink(missing_ok=True)

    inicio = time.perf_counter()
    with guion.open() as origen:
        resultado = subprocess.run(
            [str(BINARIO), str(base), "/dev/null"],
            stdin=origen,
            capture_output=True,
            text=True,
            check=False,
        )
    transcurrido = time.perf_counter() - inicio

    if resultado.returncode != 0:
        print(resultado.stdout[-2000:], file=sys.stderr)
        raise SystemExit("Falló la carga de vectores")

    return base, transcurrido, base.stat().st_size


def medir(base: pathlib.Path, k: int, consultas: int, destino: pathlib.Path):
    ejecutar(f".knncsv {destino} {k} {consultas}\n", base)


def configuraciones(rapido: bool):
    """La matriz experimental: se varía un factor a la vez."""
    vectores = [1000, 5000] if rapido else VECTORES
    dimensiones = [16, 64] if rapido else DIMENSIONES
    ks = [1, 10] if rapido else KS
    consultas = 10 if rapido else CONSULTAS

    vistas = set()

    # Experimento 1: efecto del número de vectores.
    for n in vectores:
        vistas.add((n, DIMENSION_BASE, K_BASE))
    # Experimento 2: efecto de la dimensionalidad.
    for d in dimensiones:
        vistas.add((VECTORES_BASE if not rapido else 5000, d, K_BASE))
    # Experimento 3: efecto de k.
    for k in ks:
        vistas.add((VECTORES_BASE if not rapido else 5000, DIMENSION_BASE, k))

    return sorted(vistas), consultas


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rapido", action="store_true", help="matriz reducida, para comprobar")
    args = parser.parse_args()

    if not BINARIO.exists():
        raise SystemExit(
            f"No existe {BINARIO}. Compile primero:\n"
            "  cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release\n"
            "  cmake --build build-release -j\"$(nproc)\""
        )

    RESULTADOS.mkdir(parents=True, exist_ok=True)
    crudos = RESULTADOS / "resultados_crudos.csv"
    carga = RESULTADOS / "carga.csv"

    # Los CSV se regeneran, nunca se editan a mano.
    crudos.unlink(missing_ok=True)
    carga.unlink(missing_ok=True)

    combinaciones, consultas = configuraciones(args.rapido)
    print(f"{len(combinaciones)} configuraciones, {consultas} consultas por estrategia")

    with carga.open("w", newline="") as fichero:
        escritor = csv.writer(fichero)
        escritor.writerow(
            ["vectores", "dimension", "segundos_carga", "bytes_archivo", "paginas"]
        )

        for vectores, dimension, k in combinaciones:
            print(f"  n={vectores:>6} d={dimension:>4} k={k:>3} ... ", end="", flush=True)
            base, segundos, tamano = cargar(vectores, dimension)
            escritor.writerow([vectores, dimension, f"{segundos:.3f}", tamano, tamano // 4096])
            fichero.flush()

            medir(base, k, consultas, crudos)
            print(f"{segundos:.1f} s de carga, {tamano // 1024} KiB")

            # Los archivos de datos se conservan solo si son pequeños: la matriz
            # completa ocuparía cientos de megabytes.
            if tamano > 32 * 1024 * 1024:
                base.unlink(missing_ok=True)

    print(f"\nResultados crudos: {crudos}")
    print(f"Métricas de carga: {carga}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
