.SILENT: all install clean
C=gcc
CFLAGS=-Wall -Os -fpie

all: virtual_controller.c
	$(C) $(CFLAGS) virtual_controller.c -o virtual_controller

install:
	strip --strip-unneeded virtual_controller
	cp virtual_controller /sbin/virtual_controller

clean:
	rm -f virtual_controller
