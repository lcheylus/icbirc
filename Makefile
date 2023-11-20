CC = gcc
CFLAGS = -Wall -Werror -Wstrict-prototypes

LIBS = -lbsd

DEPS = src/icb.h src/irc.h
OBJ = src/icbirc.c src/icb.c src/irc.c

.PHONY: clean install

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

icbirc: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

install:
	cp -v icbirc /usr/local/bin

	mkdir -p /usr/local/share/man/man8/
	cp -v man/icbirc.8 /usr/local/share/man/man8/

clean:
	rm -f icbirc *.o
