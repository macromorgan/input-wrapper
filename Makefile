.SILENT: all install clean
C=gcc
CFLAGS=-Os -std=gnu11 -Wall -Wextra -Wformat-security -Werror
SECURITY_FLAGS=-Wstack-protector -Wstack-protector --param ssp-buffer-size=4 \
	       --param ssp-buffer-size=4 -fstack-protector-strong \
	       -fstack-clash-protection -pie -fPIE -D_FORTIFY_SOURCE=2

all: virtual_controller.c
	$(C) $(CFLAGS) $(SECURITY_FLAGS) virtual_controller.c -o virtual_controller

install:
	strip --strip-unneeded virtual_controller
	cp virtual_controller /sbin/virtual_controller

clean:
	rm -f virtual_controller
