CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
LDFLAGS ?= -Wl,-z,relro,-z,now
PREFIX  ?= /usr/local

all: swtch

swtch: src/swtch.c
	$(CC) $(CFLAGS) $(LDFLAGS) src/swtch.c -o swtch

install: swtch
	install -Dm755 swtch $(DESTDIR)$(PREFIX)/bin/swtch
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/swtch/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/swtch
	rm -f $(DESTDIR)$(PREFIX)/share/licenses/swtch/LICENSE

clean:
	rm -f swtch

.PHONY: all install uninstall clean
