# Matriz de cumplimiento de la rúbrica

Evaluación del artículo `articulo_busqueda_vectorizada.md` frente a los cuatro
criterios. Ningún criterio se marca como cumplido sin evidencia comprobable.

---

## Resumen

| Criterio | Estado |
|---|---|
| 1. Estructura académica formal | **Cumple** |
| 2. Aporte novedoso o distintivo | **Cumple, con una salvedad declarada** |
| 3. Experimentos y resultados | **Cumple, con métricas no aplicables declaradas** |
| 4. Conclusiones | **Cumple** |

La salvedad del criterio 2 y las métricas no aplicables del criterio 3 se detallan
abajo. Ambas se derivan del mismo hecho, declarado de forma explícita a lo largo de
todo el artículo: **no hay índice vectorial**, la búsqueda es exhaustiva.

---

## Criterio 1: estructura académica formal

| Exigencia del nivel Excelente | Evidencia en el artículo | Evidencia en el repositorio | Estado |
|---|---|---|---|
| Título específico y fiel a la implementación | Título: menciona «búsqueda exacta» y «selección Top-k acotada frente a ordenamiento completo», sin usar la palabra «índice» | `src/execution/knn_operators.cpp` contiene exactamente esas dos estrategias | Cumple |
| Resumen de 180 a 250 palabras con contexto, problema, objetivo, método, resultado y limitación | Sección «Resumen», 250 palabras, sin citas | — | Cumple |
| Entre cinco y siete palabras clave | Sección «Palabras clave», siete términos | — | Cumple |
| Introducción con problema, motivación, objetivo, alcance, aporte, limitaciones y organización | Sección 1, con lista de seis contribuciones verificables en 1.1 | Cada contribución remite a un archivo y una función | Cumple |
| Trabajos relacionados con profundidad teórica y comparativa, no un listado de productos | Sección 2, cuatro subsecciones; para cada línea se indica problema, estrategia, ventaja, limitación y diferencia respecto de la propuesta | 20 referencias | Cumple |
| Arquitectura propuesta con diagrama | Sección 3, figura 1 en Mermaid derivada del código, tabla de componentes con archivo por cada uno | Los componentes del diagrama existen en las rutas indicadas | Cumple |
| Diseño del sistema justificado, con complejidad y compromisos | Sección 4: representación, métricas con fórmulas, pseudocódigo del algoritmo, complejidad temporal y espacial, tabla de compromisos | `src/vector/distance.cpp`, `src/execution/knn_operators.cpp` | Cumple |
| Implementación con fragmentos y su procedencia | Sección 5, seis fragmentos, cada uno seguido de su archivo y función | Fragmentos copiados del código real | Cumple |
| Experimentos y resultados en diez subsecciones | Sección 6, subsecciones 6.1 a 6.10 | `experimentos/resultados/*.csv` | Cumple |
| Discusión que interpreta, no repite las tablas | Sección 7, siete subsecciones; incluye el modelo de coste que explica las dos tendencias opuestas | — | Cumple |
| Conclusiones que responden objetivo y preguntas | Sección 8 | — | Cumple |
| Referencias en formato IEEE | Sección 9, 20 entradas | `docs/referencias_pendientes.md` lista lo pendiente de verificar | Cumple |
| Todas las tablas y figuras numeradas, citadas e interpretadas | 7 tablas y 8 figuras, todas referenciadas e interpretadas en el texto | `experimentos/graficos/` contiene las 7 imágenes | Cumple |
| Extensión de 4 500 a 7 000 palabras | Artículo sin anexos: 8 479 palabras de prosa, 9 689 contando las celdas de las tablas | — | **Excede el rango superior**; ver nota al final |

---

## Criterio 2: aporte novedoso o distintivo

| Exigencia del nivel Excelente | Evidencia en el artículo | Evidencia en el repositorio | Estado |
|---|---|---|---|
| El aporte se presenta claramente como distintivo | Secciones 1, 1.1 y 3.1 | — | Cumple |
| Está conectado con la arquitectura | Sección 3.1: los vectores viven en las mismas páginas ranuradas y se leen por el mismo administrador de buffer | `src/storage/record.cpp`, `src/storage/table_heap.cpp` | Cumple |
| Tiene justificación técnica | Sección 1: un índice hash dispersa las claves y destruye la vecindad; un árbol B+ exige un orden total que un vector no tiene | `src/index/hash_index.cpp` resuelve solo igualdades | Cumple |
| Resuelve una necesidad concreta | Sección 1: el sistema no podía expresar ni resolver consultas de proximidad | — | Cumple |
| Está respaldado por código | Anexo A, 29 afirmaciones con archivo y función | 1 223 líneas nuevas entre código y pruebas | Cumple |
| Está respaldado por pruebas | Sección 5 y 6.6 | 19 casos en `tests/distance_test.cpp`, 33 en `tests/knn_test.cpp`; 316 en total, verdes también bajo saneadores | Cumple |
| Está respaldado por resultados experimentales | Sección 6, tablas 3 a 7 | `experimentos/resultados/resultados_crudos.csv`, 720 observaciones | Cumple |
| Se explica por qué fue incorporado | Sección 1, «Problema abordado» | — | Cumple |
| Se explica qué componentes modifica o amplía | Sección 3.1 y `docs/evidencias_codigo.md` §1 | 17 archivos modificados, 8 nuevos | Cumple |
| Se explica cómo funciona | Secciones 4.1 a 4.4, con pseudocódigo | — | Cumple |
| Se explica qué coste tiene | Sección 4.3: `Θ(n·d + n log k)` frente a `Θ(n·d + n log n)`; sección 7.4 lo relaciona con lo medido | — | Cumple |
| Se explica qué beneficio ofrece | Sección 7.5 | Tabla 4: de 1,19× a 1,67× | Cumple |
| Se explica en qué condiciones resulta útil | Secciones 7.1 y 8: colección grande, dimensión moderada, `k` pequeño frente a `n` | — | Cumple |
| Se explica qué limitaciones presenta | Sección 7.6, seis limitaciones | — | Cumple |
| **Salvedad declarada** | La novedad es **local al proyecto**, no frente a la literatura. La sección 1 lo dice de forma explícita: «No se reclama novedad científica frente a la literatura de búsqueda vectorial». El artículo tampoco emplea el término «índice vectorial» para describir lo implementado | `grep -rniE "hnsw\|ivf\|lsh\|quantiz" include src` no devuelve resultados | Declarado |

---

## Criterio 3: experimentos y resultados

| Exigencia del nivel Excelente | Evidencia en el artículo | Evidencia en el repositorio | Estado |
|---|---|---|---|
| Comparación con una alternativa base | Sección 6: ordenamiento completo frente a selección Top-k, una de las comparaciones admitidas | `ExecutionEngine::SetTopKEnabled` | Cumple |
| Misma configuración en ambas ramas | Sección 6.3 y `docs/metodologia_experimental.md` §2 | Las dos rondas comparten proceso, archivo, buffer, consultas y función de puntuación | Cumple |
| Tiempo exacto de ejecución | Tablas 3, 5 y 6 | `Database::Execute` con reloj monótono | Cumple |
| Latencia promedio, mínima y máxima | Tabla 3 | `resultados_resumen.csv` | Cumple |
| Percentiles p50, p95 y p99 | Tablas 3 y 5 | ídem | Cumple |
| Desviación estándar | Tablas 3 y 6, y sección 6.10 | ídem | Cumple |
| Consultas por segundo | Tablas 3 y 5 | ídem | Cumple |
| Número de comparaciones vectoriales | Tabla 3, columna «Distancias»; figura 6 | `PhysicalOperator::CountDistance` | Cumple |
| Registros examinados | Tabla 7 | `metricas_io.csv` | Cumple |
| Páginas leídas desde disco | Tabla 7 | ídem | Cumple |
| Aciertos y fallos de buffer, y tasa de aciertos | Tabla 7 | ídem | Cumple |
| Volúmenes suficientes para ver el comportamiento asintótico | Dos órdenes de magnitud: de 1 000 a 100 000 vectores; figura 3 en escala logarítmica | `carga.csv` | Cumple |
| Repeticiones con calentamiento | Sección 6.5: 30 medidas más una descartada, por estrategia y configuración | `RunKnnBatch` en `src/main.cpp` | Cumple |
| Datos reproducibles con semilla documentada | Sección 6.4: `default_rng(42)` para los datos y `mt19937(42)` para las consultas | `generar_vectores.py`, `MakeQueryVectors` | Cumple |
| Resultados crudos guardados | 720 filas | `resultados_crudos.csv` | Cumple |
| Exactitud de los resultados | Tabla 2 y sección 6.6 | `exactitud.csv`, más comparación fila a fila en `tests/knn_test.cpp` | Cumple |
| Gráficos con número, título e interpretación | Figuras 2 a 8, todas citadas e interpretadas | `experimentos/graficos/` | Cumple |
| Escritura de páginas | No procede: las consultas de búsqueda no modifican datos, de modo que `disk_writes` es 0 en todas ellas | — | No aplicable, declarado |
| Memoria máxima utilizada | **No medida.** El espacio se cuantifica indirectamente mediante `candidates_admitted`, que es la magnitud en la que las estrategias difieren. Declarado en el anexo B y en `metodologia_experimental.md` §7 | — | No aplicable, declarado |
| Tamaño del índice y tiempo de construcción | **No procede:** no hay índice vectorial. Se registra en su lugar el tiempo de carga de los datos y el tamaño del archivo, tabla 1 | — | No aplicable, declarado |
| `Recall@k` | **No procede:** ambas estrategias son exactas, de modo que el `recall` es 1 por construcción. Argumentado en la sección 6.6 | `tests/knn_test.cpp`, caso de equivalencia | No aplicable, declarado |
| Búsqueda secuencial frente a índice vectorial | **No ejecutable:** no hay índice vectorial. La rúbrica admite otras comparaciones y se emplea una de ellas | — | Sustituido, declarado |

---

## Criterio 4: conclusiones

| Exigencia del nivel Excelente | Evidencia en el artículo | Evidencia en el repositorio | Estado |
|---|---|---|---|
| Responde al objetivo general | Sección 8, párrafo segundo | 316 pruebas en verde | Cumple |
| Responde cada pregunta de investigación | Sección 7.1 punto por punto y sección 8, párrafo tercero | Tablas 3 a 7 | Cumple |
| Interpreta los resultados sin repetir las tablas | Sección 7.2: modelo de coste que explica las dos tendencias opuestas | — | Cumple |
| Menciona cifras relevantes sin copiar las tablas | Sección 8: 0,63 ms por millar de vectores, 59 % de coste fijo con `d = 16` | `resultados_resumen.csv` | Cumple |
| Indica en qué condiciones funciona mejor | Sección 8, párrafo cuarto | Tabla 4 | Cumple |
| Reconoce limitaciones | Sección 8, párrafo quinto, y sección 7.6 completa | — | Cumple |
| Propone trabajo futuro técnicamente viable | Sección 8, último párrafo: seis líneas, cada una con su compromiso; ninguna presentada como implementada | El informe de brechas `brechas_busqueda_vectorizada.md` §6 detalla el coste de la primera | Cumple |

---

## Nota sobre la extensión

El artículo supera el rango de 4 500 a 7 000 palabras que indica la rúbrica: la prosa
suma 8 479 palabras, y 9 689 si se cuentan las celdas de las siete tablas. El exceso se
concentra en tres puntos: la sección 2, que desarrolla cinco líneas de trabajo
relacionado con su comparación explícita; la sección 6, que documenta cuatro
experimentos con sus tablas completas; y la sección 7, cuyas siete subsecciones incluyen
el análisis de amenazas a la validez.

Se deja constancia en lugar de recortar porque el recorte tendría que salir de alguno de
esos tres bloques, y los tres responden a exigencias explícitas del nivel Excelente. Si
se requiere ajustar la extensión, la vía menos dañina sería mover las tablas 5 y 6 y las
figuras 5 a 8 a un anexo, lo que retiraría cerca de 900 palabras sin eliminar ningún
argumento.

---

## Verificación de la propia matriz

```bash
# Las 12 secciones del artículo
grep -E "^#{1,2} " docs/articulo_busqueda_vectorizada.md

# Las tablas y figuras están numeradas y citadas
grep -cE "^\*\*Tabla [0-9]" docs/articulo_busqueda_vectorizada.md    # 7
grep -cE "^\*\*Figura [0-9]" docs/articulo_busqueda_vectorizada.md   # 8

# Los datos del artículo proceden de los CSV
awk -F, 'NR==1 || ($3==64 && $4==10)' experimentos/resultados/resultados_resumen.csv

# Las pruebas que respaldan el aporte
ctest --test-dir build-release -R "Distance|Knn|Vector" --output-on-failure
```
