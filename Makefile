PKGS=sdl2 gl
CFLAGS=-std=c99 $(shell pkg-config --cflags $(PKGS)) -Inanovg/src
LIBS=-Lnanovg/build -lnanovg -lm $(shell pkg-config --libs $(PKGS))

all: main

main.o: main.c
	$(CC) $(CFLAGS) -c $<

main: main.o
	$(CC) $^ -o $@ $(LIBS)

clean:
	rm -f *.o main
