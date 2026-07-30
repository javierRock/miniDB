#!/usr/bin/env python3
"""Genera el guion SQL que carga una tabla de vectores en el Mini-SGBD.

Los vectores son sintéticos: componentes uniformes en [0, 1) con semilla fija.
No representan textos ni imágenes reales, y ninguna conclusión del artículo
depende de que lo hicieran: lo que se mide es el coste de recorrer y ordenar n
vectores de dimensión d, que es independiente de su significado.

Uso:
    python3 generar_vectores.py --vectores 10000 --dimension 64 > carga.sql
"""

import argparse
import sys

import numpy as np

SEMILLA = 42


def generar(vectores: int, dimension: int, tabla: str, semilla: int):
    """Emite el CREATE TABLE y un INSERT por vector."""
    rng = np.random.default_rng(semilla)

    print(
        f"CREATE TABLE {tabla} ("
        f"id INT PRIMARY KEY, "
        f"etiqueta VARCHAR(20), "
        f"emb VECTOR({dimension}));"
    )

    # Se genera por bloques para no materializar n*d floats de una vez cuando n
    # es grande.
    bloque = 1000
    for inicio in range(0, vectores, bloque):
        cantidad = min(bloque, vectores - inicio)
        datos = rng.random((cantidad, dimension), dtype=np.float32)
        for fila in range(cantidad):
            identificador = inicio + fila + 1
            componentes = ",".join(f"{x:.6f}" for x in datos[fila])
            print(
                f"INSERT INTO {tabla} VALUES "
                f"({identificador}, 'v{identificador}', [{componentes}]);"
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vectores", type=int, required=True)
    parser.add_argument("--dimension", type=int, required=True)
    parser.add_argument("--tabla", default="docs")
    parser.add_argument("--semilla", type=int, default=SEMILLA)
    args = parser.parse_args()

    if args.vectores <= 0 or args.dimension <= 0:
        print("El número de vectores y la dimensión deben ser positivos", file=sys.stderr)
        return 1

    generar(args.vectores, args.dimension, args.tabla, args.semilla)
    return 0


if __name__ == "__main__":
    sys.exit(main())
