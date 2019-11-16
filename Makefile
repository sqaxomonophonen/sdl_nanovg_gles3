CFLAGS=-Wall -std=c99

all: main

svg2nvg.o: svg2nvg.c
	$(CC) $(CFLAGS) -Iyxml -c $<

svg2nvg: svg2nvg.o
	$(CC) $^ -o $@ -Lyxml -lyxml

drawing.inc.h: drawing.svg svg2nvg
	./svg2nvg $< $@

nanovg_gl.o: nanovg_gl.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags gl) -Inanovg/src -c $<

stb_sprintf.o: stb_sprintf.c
	$(CC) $(CFLAGS) -c $<

main.o: main.c drawing.inc.h
	$(CC) $(CFLAGS) $(shell pkg-config --cflags gl sdl2) -Inanovg/src -c $<

main: main.o nanovg_gl.o stb_sprintf.o
	$(CC) $^ -o $@ -Lnanovg/build -lnanovg -lm $(shell pkg-config --libs gl sdl2)

clean:
	rm -f *.o main svg2nvg *.inc.h
