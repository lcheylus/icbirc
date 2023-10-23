CC = gcc
CFLAGS = -Wall -Werror -Wstrict-prototypes

LIBS = -lbsd

DEPS = icb.h irc.h
OBJ = icbirc.c icb.c irc.c

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

icbirc: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f icbirc *.o
