# Referencias pendientes de verificación

El artículo cita 20 referencias. Todas corresponden a trabajos que existen y cuyos
autores, títulos, publicaciones y años se han indicado según el conocimiento
disponible. Este documento separa lo que puede darse por firme de lo que **debe
comprobarse contra la fuente original antes de un envío real**.

Ninguna referencia fue inventada. Lo que puede contener errores son los datos
bibliográficos secundarios —números de página, de volumen y de número— que no se han
podido cotejar con la publicación desde este entorno, sin acceso a las bases de datos
de ACM ni de IEEE.

---

## 1. Datos que deben comprobarse

| Ref. | Trabajo | Qué comprobar |
|---|---|---|
| [1] | Bentley, árboles k-d, *CACM* 1975 | Volumen 18, número 9, páginas 509–517 |
| [2] | Guttman, árboles R, SIGMOD 1984 | Páginas 47–57 |
| [3] | Weber, Schek y Blott, VLDB 1998 | Páginas 194–205 |
| [4] | Beyer *et al.*, ICDT 1999 | Páginas 217–235 |
| [5] | Indyk y Motwani, STOC 1998 | Páginas 604–613 |
| [6] | Gionis, Indyk y Motwani, VLDB 1999 | Páginas 518–529 |
| [7] | Jégou, Douze y Schmid, *IEEE TPAMI* 2011 | Volumen 33, número 1, páginas 117–128 |
| [8] | Malkov y Yashunin, *IEEE TPAMI* 2020 | Volumen 42, número 4, páginas 824–836 |
| [9] | Johnson, Douze y Jégou, *IEEE Trans. Big Data* 2021 | Volumen 7, número 3, páginas 535–547 |
| [11] | Wang *et al.*, Milvus, SIGMOD 2021 | Páginas 2614–2627 y la **lista completa de coautores**, que es extensa |
| [14] | Graefe, *ACM Computing Surveys* 1993 | Volumen 25, número 2, páginas 73–169 |
| [15] | Graefe, Volcano, *IEEE TKDE* 1994 | Volumen 6, número 1, páginas 120–135 |
| [17] | Effelsberg y Haerder, *ACM TODS* 1984 | Volumen 9, número 4, páginas 560–595 |
| [20] | Aumüller, Bernhardsson y Faithfull, *Information Systems* 2020 | Volumen 87; **falta el número de artículo o de páginas** |

## 2. Datos que pueden darse por firmes

| Ref. | Motivo |
|---|---|
| [10] | Identificador de arXiv comprobable directamente: arXiv:2401.08281 |
| [12] | Repositorio público de `pgvector`; la dirección puede visitarse |
| [13] | Documentación oficial de Qdrant; la dirección puede visitarse |
| [16] | MonetDB/X100 en CIDR 2005; las actas de CIDR no tienen paginación formal, de modo que la cita sin páginas es correcta |
| [18] | Libro con edición y editorial indicadas; la séptima edición de 2019 existe |
| [19] | Libro; la tercera edición de 2003 existe |

## 3. Referencias que se consideraron y no se incluyeron

Para dejar constancia de que la selección fue deliberada:

- **Modelos que producen los vectores** (word2vec, BERT y sucesores). No se citan
  porque el trabajo no genera *embeddings* ni evalúa su calidad: los datos son
  sintéticos y el artículo lo declara. Citarlos sugeriría una conexión con
  representaciones reales que el experimento no tiene.
- **Extensiones vectoriales de SQLite.** Se mencionaban en el guion de trabajo, pero no
  se ha podido verificar cuál de las varias en circulación sería la cita apropiada ni
  su estado de publicación, de modo que se omitieron en lugar de citarlas de forma
  imprecisa.
- **Trabajos sobre SGBD didácticos concretos** (por ejemplo proyectos docentes
  universitarios). Se omitieron por la misma razón: no se ha podido verificar una
  publicación citable, y una referencia a un repositorio docente sin publicación
  asociada no aportaría al argumento.

## 4. Cómo completar la verificación

1. Localizar cada trabajo por su título en el portal de la editorial (ACM Digital
   Library para SIGMOD, TODS y Computing Surveys; IEEE Xplore para TPAMI, TKDE y
   Transactions on Big Data; ScienceDirect para *Information Systems*).
2. Copiar volumen, número, páginas y año de la ficha oficial, no de un agregador.
3. Añadir el DOI, que el artículo no incluye por no haberse podido verificar.
4. Comprobar la lista de coautores de [11], que en la publicación original es larga y
   se ha transcrito de memoria.
5. Sustituir en `docs/articulo_busqueda_vectorizada.md`, sección 9, y retirar de este
   documento cada entrada verificada.

Mientras una referencia siga en la sección 1 de este documento, debe leerse en el
artículo como acompañada implícitamente de la marca
`[REFERENCIA ACADÉMICA PENDIENTE DE VERIFICACIÓN]` en sus datos secundarios. El trabajo,
los autores principales, el título, la publicación y el año no están en duda; los
números, sí.
