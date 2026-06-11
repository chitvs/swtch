CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?= -Wl,-z,relro,-z,now
PREFIX ?= /usr/local

all: sw

sw: sw.c
	$(CC) $(CFLAGS) sw.c -o sw

install: sw
	install -Dm755 sw $(DESTDIR)$(PREFIX)/bin/sw
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/sw-stopwatch/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/sw
	rm -f $(DESTDIR)$(PREFIX)/share/licenses/sw-stopwatch/LICENSE

clean:
	rm -f sw

.PHONY: all install uninstall clean
