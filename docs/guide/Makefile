# Builds the FujiNet Palm Guide: make -> fujinet-palm-guide.pdf
# Needs XeLaTeX (with tcolorbox, titlesec, enumitem, fontawesome5), ImageMagick,
# and the macOS fonts Avenir Next, Charter and Menlo.
GUIDE  = fujinet-palm-guide
SHOTS  = $(wildcard screens/*.png)
BIG    = $(SHOTS:screens/%.png=build/screens/%.png)

$(GUIDE).pdf: $(GUIDE).tex $(BIG) art/fujinet-logo.pdf
	mkdir -p build
	xelatex -interaction=nonstopmode -halt-on-error -output-directory=build $(GUIDE).tex >/dev/null
	xelatex -interaction=nonstopmode -halt-on-error -output-directory=build $(GUIDE).tex >/dev/null
	cp build/$(GUIDE).pdf $@

# Palm screens are 160x160; scale them 4x without smoothing so they print crisp.
build/screens/%.png: screens/%.png
	@mkdir -p build/screens
	magick $< -filter point -resize 400% $@

clean:
	rm -rf build

.PHONY: clean
