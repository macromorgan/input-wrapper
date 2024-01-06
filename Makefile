.SILENT: all clean
C=gcc
CFLAGS=-c -Wall -Os

all: wrap.c
	$(C) wrap.c -o wrap

clean:
	rm -f wrap
