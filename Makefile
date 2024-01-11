.SILENT: all install clean
C=gcc
CFLAGS=-Wall -Os -fpie

all: wrap.c
	$(C) $(CFLAGS) wrap.c -o wrap

install:
	strip --strip-unneeded wrap
	cp wrap /sbin/wrap

clean:
	rm -f wrap
