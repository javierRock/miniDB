-- Demostración de MiniDB.
-- Se ejecuta con:  ./build/minidb data/minidb.db < examples/demo.sql

CREATE TABLE students (
    id INT PRIMARY KEY,
    name VARCHAR(50),
    age INT,
    career VARCHAR(50)
);

INSERT INTO students VALUES (1, 'Ana', 20, 'Ciencia de la Computación');
INSERT INTO students VALUES (2, 'Luis', 22, 'Ingeniería de Sistemas');
INSERT INTO students VALUES (9, 'María', 19, 'Ingeniería de Software');
INSERT INTO students VALUES (4, 'Diego', 25, 'Ingeniería de Sistemas');
INSERT INTO students VALUES (5, 'Elena', 22, 'Ciencia de la Computación');

-- Escaneo secuencial: no hay filtro, se recorren todas las páginas.
SELECT * FROM students;

-- Búsqueda por clave primaria: el planificador elige el índice hash.
SELECT * FROM students WHERE id = 1;

-- Filtro sobre una columna sin índice: escaneo secuencial más filtro.
SELECT * FROM students WHERE age >= 20;

-- Proyección real de columnas.
SELECT id, name FROM students WHERE career = 'Ingeniería de Sistemas';

-- Ordenamiento: SortOperator es bloqueante, materializa antes de emitir.
SELECT * FROM students ORDER BY age DESC;

-- Se puede ordenar por una columna que no se proyecta.
SELECT name FROM students ORDER BY age;

-- Agrupamiento con conteo: AggregateOperator, también bloqueante.
SELECT career, COUNT(*) FROM students GROUP BY career;

-- Ordenar el resultado del agrupamiento: Sort por encima de Aggregate.
SELECT career, COUNT(*) FROM students GROUP BY career ORDER BY COUNT(*) DESC;

-- Conteo global, sin agrupar.
SELECT COUNT(*) FROM students;

-- El filtro se aplica antes de agrupar.
SELECT career, COUNT(*) FROM students WHERE age >= 20 GROUP BY career;

-- Una clave primaria duplicada se rechaza sin tocar la tabla.
INSERT INTO students VALUES (1, 'Duplicada', 30, 'X');

UPDATE students SET age = 21 WHERE id = 1;

DELETE FROM students WHERE id = 2;

SELECT * FROM students;

-- Evaluación: la misma búsqueda por clave con y sin índice.
SELECT * FROM students WHERE id = 9;
.indice off
SELECT * FROM students WHERE id = 9;
.indice on

-- Los dos modelos de ejecución sobre el mismo escaneo con filtro. Compare el
-- número de llamadas a Next() y de accesos al Buffer Pool en los dos planes.
SELECT * FROM students WHERE age >= 20;
.vectorizado on
SELECT * FROM students WHERE age >= 20;
.vectorizado off

.schema
.pages
.buffer
.files

-- ---------------------------------------------------------------------------
-- Búsqueda por similitud vectorial sobre una segunda base de datos.
-- Este guion opera sobre una sola tabla, así que el ejemplo vectorial completo
-- vive en scripts/demo_vectorial.sh, que crea su propia base de datos.
-- ---------------------------------------------------------------------------
