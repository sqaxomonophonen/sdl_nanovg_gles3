PKGS=sdl2 gl
CFLAGS=-Wall -std=c99

all: main

svg2nvg.o: svg2nvg.c
	$(CC) $(CFLAGS) -Iyxml -c $<

svg2nvg: svg2nvg.o
	$(CC) $^ -o $@ -Lyxml -lyxml

drawing.inc.h: drawing.svg svg2nvg
	./svg2nvg $< $@

main.o: main.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags $(PKGS)) -Inanovg/src -c $<

main: main.o
	$(CC) $^ -o $@ -Lnanovg/build -lnanovg -lm $(shell pkg-config --libs $(PKGS))

clean:
	rm -f *.o main *.inc.h
