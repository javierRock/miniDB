# Metodología experimental

Detalle de cómo se obtuvieron los números de la sección 6 del artículo, con las
decisiones metodológicas y sus motivos. Complementa, no repite, el anexo C.

---

## 1. Qué se compara y por qué

Se comparan dos estrategias de **ranking** dentro de una búsqueda exacta y exhaustiva
de los `k` vecinos más cercanos:

| | Línea base | Propuesta |
|---|---|---|
| Operador | `KnnFullSortOperator` | `KnnScanOperator` |
| Ranking | Ordena las `n` puntuaciones y toma las `k` primeras | Montículo de máximos acotado a `k` |
| Tiempo | `Θ(n·d) + O(n log n)` | `Θ(n·d) + O(n log k)` |
| Espacio | `O(n)` candidatos | `O(k)` candidatos |
| Exactitud | Exacta | Exacta |

Esta comparación corresponde a la opción «ordenamiento completo frente a selección
Top-k» de las admitidas por la rúbrica. **No** se compara búsqueda secuencial frente a
índice vectorial, ni exacta frente a aproximada, porque el sistema no implementa un
índice vectorial ni búsqueda aproximada: esas comparaciones no podrían ejecutarse y
presentarlas sería inventarlas.

---

## 2. Control de variables

Todo lo que no es la estrategia de ranking se mantiene fijo:

- **Mismo proceso.** Las dos rondas se ejecutan en la misma invocación del binario, de
  modo que no puede haber diferencias de estado del sistema entre ellas.
- **Mismo archivo de datos.** No se recarga ni se regenera entre rondas.
- **Mismas consultas.** Los vectores de consulta se generan dentro del binario con
  `std::mt19937` y semilla 42 (`src/main.cpp`, función `MakeQueryVectors`), y la misma
  secuencia se pasa a las dos rondas. Generarlos en el binario y no en el guion evita
  que la reproducibilidad dependa de la versión de una biblioteca externa.
- **Misma configuración de buffer.** Ocho marcos en ambas.
- **Misma compilación.** Un único binario `Release`.
- **Misma métrica y mismo `k`.**
- **Misma función de puntuación.** Las dos estrategias comparten
  `KnnScanOperator::Score`, de forma que la aritmética medida es literalmente el mismo
  código.

Lo único que cambia es el operador que el planificador construye, gobernado por
`ExecutionEngine::SetTopKEnabled`.

---

## 3. Datos

**Generación.** `experimentos/scripts/generar_vectores.py`, con
`numpy.random.default_rng(42)`; componentes independientes y uniformes en `[0, 1)`,
`float32`; emitidos como literales con seis decimales.

**Esquema.** `docs (id INT PRIMARY KEY, etiqueta VARCHAR(20), emb VECTOR(d))`. La
columna `etiqueta` está presente a propósito: hace que el registro contenga algo más
que el vector, que es la situación realista y la que hace visible el coste por registro
independiente de la dimensión.

**Advertencia sobre la naturaleza de los datos.** Son sintéticos y uniformes. No
representan textos ni imágenes reales, y el artículo no afirma que lo hagan. Lo que se
mide —el coste de recorrer y ordenar `n` vectores de dimensión `d`— es independiente
del significado de los datos, pero la *distribución* sí afecta a la frecuencia con que
un candidato mejora la raíz del montículo, y por tanto a la magnitud de la aceleración.
Está discutido como amenaza a la validez externa en la sección 7.7 del artículo.

**Volúmenes.** Acotados por el formato físico. Un vector ocupa `2 + 4d` bytes y una
página ofrece 4084 para directorio y datos:

| Dimensión | Bytes por vector | Registros por página |
|---:|---:|---:|
| 16 | 66 | 58 |
| 64 | 258 | 15 |
| 256 | 1 026 | 3 |
| 768 | 3 074 | **1** |

Con `d = 768` cabría un registro por página y el escaneo degeneraría en una lectura de
página por registro: el experimento mediría el subsistema de archivos, no la búsqueda.
Por eso el rango evaluado llega a 256. Es una desviación consciente de los valores
orientativos de la rúbrica, justificada por esta tabla.

---

## 4. Procedimiento de medición

Para cada configuración:

1. Se crea la base de datos desde cero y se carga el guion de vectores. Se registra el
   tiempo de carga y el tamaño del archivo.
2. Se activa la estrategia a medir.
3. **Calentamiento:** se ejecuta una consulta cuyo resultado se descarta. Con ocho
   marcos de buffer, la primera consulta paga la lectura de páginas que las siguientes
   encuentran residentes, y sin descartarla el mínimo estaría sesgado.
4. Se ejecutan 30 consultas medidas, registrando por cada una: latencia, distancias
   calculadas, lecturas físicas de página, aciertos, fallos y filas devueltas.
5. Se repite desde el paso 2 con la otra estrategia.

Resultado: 12 configuraciones × 2 estrategias × 30 consultas = **720 observaciones
crudas**, en `experimentos/resultados/resultados_crudos.csv`.

---

## 5. Estadísticas

Calculadas en `experimentos/scripts/procesar_resultados.py`:

- Media aritmética, mínimo, máximo y desviación estándar de población.
- Percentiles p50, p95 y p99 **por rango más cercano** sobre la muestra ordenada, sin
  interpolar: con 30 observaciones, interpolar sugeriría una precisión que la muestra
  no tiene.
- Consultas por segundo, derivadas de la media.
- Media de distancias, lecturas de página, aciertos y fallos por consulta.
- Tasa de aciertos como `aciertos / (aciertos + fallos)`.

Los CSV se regeneran íntegramente en cada ejecución. Ninguno se edita a mano.

---

## 6. Verificación de exactitud

Precede a toda medición de rendimiento y está separada de ella.

**Casos calculables a mano** (`tests/distance_test.cpp`): distancia euclidiana sobre el
triángulo 3-4-5, coseno a 0°, 45°, 90° y 180°, producto punto de `[1,2,3]·[4,5,6] = 32`,
y la identidad `d_cos = 1 − s_cos`.

**Casos límite** (`tests/knn_test.cpp`): vectores duplicados y empates, vector idéntico
a uno almacenado, componentes negativas, vector nulo almacenado y de consulta, tabla
vacía, dimensiones incompatibles, `k = 0`, `k > n`, y persistencia bit a bit tras
cerrar y reabrir el archivo.

**Equivalencia entre estrategias.** Ocho consultas con distintos `k` y distintas
métricas se ejecutan por los dos caminos y se comparan **fila a fila**, no solo por
recuento. Es la propiedad de la que depende toda la validez de la comparación de
rendimiento: si las dos estrategias no dan el mismo resultado, comparar sus tiempos no
significa nada.

**Comprobación agregada** (`exactitud.csv`): en las doce configuraciones
experimentales, ambas estrategias devolvieron el mismo número de filas y calcularon el
mismo número de distancias. El guion de agregación termina con código de error si
alguna configuración no coincide.

**Sobre `Recall@k`.** No se calcula, y no por omisión: `Recall@k` cuantifica lo que
pierde una búsqueda aproximada frente a la exacta. Aquí las dos estrategias son
exactas, de modo que el `recall` de ambas es 1 por construcción y la métrica no aporta
información. Tendría sentido en el momento en que se implementara un índice
aproximado, y entonces la búsqueda exacta de este trabajo serviría de referencia.

---

## 7. Qué no se midió, y por qué

| Métrica pedida por la rúbrica | Motivo |
|---|---|
| `Recall@k` | No hay búsqueda aproximada; sería 1 por construcción |
| Tamaño del índice | No hay índice vectorial |
| Tiempo de construcción del índice | Ídem. Se registra en su lugar el tiempo de carga de los datos |
| Nodos de índice visitados | Ídem |
| Memoria máxima del proceso | No instrumentada. El espacio se mide indirectamente mediante los candidatos retenidos, que es la magnitud en la que las dos estrategias difieren |
| Ejecución sin buffer frente a con buffer | El administrador de buffer no se puede desactivar: ninguna capa superior puede acceder al gestor de disco, y es un invariante arquitectónico verificado en integración continua |

---

## 8. Amenaza conocida no controlada

El orden de las rondas es siempre el mismo: primero la selección Top-k, después el
ordenamiento completo. Un efecto sistemático del estado de la caché del procesador
favorecería a la primera. La estabilidad de las medianas entre las treinta
repeticiones sugiere que el efecto es pequeño, pero **no se ha controlado alternando el
orden**, y queda registrado como limitación metodológica.
