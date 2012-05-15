CC = gcc
CFLAGS = $(shell gimptool-2.0 --cflags) 
LDFLAGS = -lasound $(shell gimptool-2.0 --libs)

soundfumble: soundfumble.o
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) soundfumble.o

.c.o:
	$(CC) $(CFLAGS) $(CFLAGS) -c $<

soundfumbe.c: soundfumble.h
clean: soundfumble
	$(RM) soundfumble soundfumble.o
install: soundfumble
	gimptool-2.0 --install-bin soundfumble
