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

-- Escaneo secuencial: no hay filtro, se recorren todas las páginas.
SELECT * FROM students;

-- Búsqueda por clave primaria: el planificador elige el índice hash.
SELECT * FROM students WHERE id = 1;

-- Filtro sobre una columna sin índice: escaneo secuencial más filtro.
SELECT * FROM students WHERE age >= 20;

-- Proyección real de columnas.
SELECT id, name FROM students WHERE career = 'Ingeniería de Sistemas';

-- Una clave primaria duplicada se rechaza sin tocar la tabla.
INSERT INTO students VALUES (1, 'Duplicada', 30, 'X');

UPDATE students SET age = 21 WHERE id = 1;

DELETE FROM students WHERE id = 2;

SELECT * FROM students;

.schema
.pages
.buffer
.files
