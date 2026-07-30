# Experimentos de búsqueda por similitud vectorial

Material para reproducir los resultados de
`../docs/articulo_busqueda_vectorizada.md`.

## Qué se compara

Dos estrategias de ranking dentro de una búsqueda **exacta y exhaustiva** de los `k`
vecinos más cercanos:

- **Línea base:** `KnnFullSortOperator`, que ordena las `n` distancias y toma las `k`
  primeras. `Θ(n·d) + O(n log n)` en tiempo, `O(n)` en espacio.
- **Propuesta:** `KnnScanOperator`, que mantiene un montículo acotado a `k`.
  `Θ(n·d) + O(n log k)` en tiempo, `O(k)` en espacio.

Ninguna de las dos es un índice: ambas examinan todos los registros. La comparación
«búsqueda secuencial frente a índice vectorial» **no** se realiza porque el sistema no
implementa un índice vectorial.

## Estructura

```
experimentos/
├── datos/datos_generados/     guiones SQL y bases de datos generadas (no versionados)
├── scripts/
│   ├── generar_vectores.py    vectores sintéticos con semilla 42
│   ├── ejecutar_benchmarks.py la matriz completa; escribe resultados_crudos.csv
│   ├── procesar_resultados.py agrega en resumen, métricas de E/S y exactitud
│   └── generar_graficos.py    las siete figuras del artículo
├── resultados/                CSV, regenerados por completo en cada ejecución
└── graficos/                  figuras en PNG
```

## Uso

```bash
# Requisito: binario en modo Release
cmake -S .. -B ../build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../build-release -j"$(nproc)"

# Comprobación rápida (4 configuraciones, 10 consultas)
python3 scripts/ejecutar_benchmarks.py --rapido

# Matriz completa (12 configuraciones, 30 consultas)
python3 scripts/ejecutar_benchmarks.py
python3 scripts/procesar_resultados.py
python3 scripts/generar_graficos.py
```

`procesar_resultados.py` termina con código de salida distinto de cero si alguna
configuración discrepa entre estrategias: sería un fallo de corrección, no de
rendimiento.

## Archivos de resultados

| Archivo | Contenido | Granularidad |
|---|---|---|
| `resultados_crudos.csv` | Una fila por consulta y estrategia | 720 filas |
| `resultados_resumen.csv` | Media, mínimo, máximo, desviación, p50, p95, p99, consultas por segundo | Una fila por configuración y estrategia |
| `metricas_io.csv` | Lecturas de página, aciertos, fallos, tasa de aciertos, registros examinados | ídem |
| `exactitud.csv` | Coincidencia entre estrategias | Una fila por configuración |
| `carga.csv` | Tiempo de carga, tamaño del archivo y páginas | Una fila por conjunto de datos |

## Reproducibilidad

- **Datos:** `numpy.random.default_rng(42)`, componentes uniformes en `[0, 1)`.
- **Consultas:** generadas **dentro del binario** con `std::mt19937(42)`, no por estos
  guiones, para que las dos estrategias reciban consultas idénticas byte a byte y la
  reproducibilidad no dependa de la versión de NumPy.
- **Calentamiento:** una consulta descartada por ronda.
- **Los CSV no se editan a mano** en ningún caso: se regeneran íntegros.

## Sobre los datos

Los vectores son sintéticos y uniformes. **No representan textos ni imágenes reales.**
Lo que se mide —el coste de recorrer y ordenar `n` vectores de dimensión `d`— es
independiente de su significado, pero la distribución afecta a la magnitud de la
aceleración. Está discutido como amenaza a la validez externa en la sección 7.7 del
artículo.

## Volúmenes y su justificación

El rango llega a 100 000 vectores de dimensión 64 y a dimensión 256 con 10 000
vectores, en lugar de a las dimensiones de 768 o más de los modelos de lenguaje
actuales. El motivo es el formato físico: un vector ocupa `2 + 4d` bytes y una página
ofrece 4084 para directorio y datos, de modo que con `d = 768` cabría **un registro por
página** y el experimento mediría el subsistema de archivos más que la búsqueda. La
tabla completa está en `../docs/metodologia_experimental.md` §3.
