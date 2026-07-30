LATEXMK = latexmk
ENGINE = lualatex
SOURCE = articulo.tex

.PHONY: all clean distclean

all: articulo.pdf

articulo.pdf: $(SOURCE)
	$(LATEXMK) -$(ENGINE) -interaction=nonstopmode -halt-on-error $(SOURCE)

clean:
	$(LATEXMK) -c $(SOURCE)

distclean:
	$(LATEXMK) -C $(SOURCE)
