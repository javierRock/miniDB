# Artículo científico en LaTeX

Este directorio contiene la versión editable y el PDF generado a partir de
`../articulo_busqueda_vectorizada.md`.

## Archivos

- `articulo.tex`: fuente principal en LaTeX.
- `articulo.pdf`: versión compilada.
- `Makefile`: órdenes de compilación y limpieza.

Las ocho gráficas se cargan desde `../../experimentos/graficos/`. El diagrama de
arquitectura está definido directamente en `articulo.tex` mediante TikZ, por lo
que también es editable.

## Compilación

Desde este directorio:

```bash
make
```

Para borrar archivos auxiliares sin eliminar el PDF:

```bash
make clean
```

La fuente usa LuaLaTeX para manejar correctamente el texto en español y los
símbolos matemáticos Unicode. Antes de una entrega deben completarse en
`articulo.tex` el nombre del docente, el hardware y la fecha de ejecución que el
documento original mantiene pendientes.
