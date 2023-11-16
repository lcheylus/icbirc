CC = gcc
CFLAGS = -Wall -Werror -Wstrict-prototypes

LIBS = -lbsd

DEPS = src/icb.h src/irc.h
OBJ = src/icbirc.c src/icb.c src/irc.c

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

icbirc: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f icbirc *.o
