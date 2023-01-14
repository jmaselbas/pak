# pak version
VERSION = 0.1.0

# Install paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

HDR = arg.h pak.h
SRC = pak.c
OBJ = $(SRC:.c=.o)
DISTFILES = $(SRC) $(HDR) pak.1 Makefile

all: pak

$(OBJ): $(HDR)

pak: $(OBJ)

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f pak $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/pak
	ln -fs pak $(DESTDIR)$(PREFIX)/bin/unpak
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < pak.1 > $(DESTDIR)$(MANPREFIX)/man1/pak.1
	ln -fs pak.1 $(DESTDIR)$(MANPREFIX)/man1/unpak.1

uninstall:
	rm -vf $(DESTDIR)$(PREFIX)/bin/pak
	rm -vf $(DESTDIR)$(PREFIX)/bin/unpak
	rm -vf $(DESTDIR)$(MANPREFIX)/man1/pak.1
	rm -vf $(DESTDIR)$(MANPREFIX)/man1/unpak.1

dist:
	mkdir -p pak-$(VERSION)
	cp $(DISTFILES) pak-$(VERSION)
	tar -cf pak-$(VERSION).tar pak-$(VERSION)
	gzip pak-$(VERSION).tar
	rm -rf pak-$(VERSION)

clean:
	rm -f $(OBJ) pak

.PHONY: all install uninstall dist clean
