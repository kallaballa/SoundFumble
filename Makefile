CC = gcc
CFLAGS = $(shell gimptool-2.0 --cflags) 
LDFLAGS = $(shell gimptool-2.0 --libs) $(shell pkg-config --libs alsa)

soundfumble: soundfumble.o
	$(CC) soundfumble.o -o $@ $(CFLAGS) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) $(LDFLAGS) -c $<

soundfumble.c: soundfumble.h

clean: soundfumble
	$(RM) soundfumble soundfumble.o

install: soundfumble
	gimptool-2.0 --install-bin soundfumble

uninstall: soundfumble
	gimptool-2.0 --uninstall-bin soundfumble

