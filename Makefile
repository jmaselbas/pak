# pak version
VERSION = 0.1.0

# Install paths
PREFIX = /usr/local

HDR = arg.h
SRC = pak.c
OBJ = $(SRC:.c=.o)
DISTFILES = $(SRC) $(HDR) Makefile

all: pak

pak: $(OBJ)

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f pak $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/pak
	ln -s pak $(DESTDIR)$(PREFIX)/bin/unpak

uninstall:
	rm -vf $(DESTDIR)$(PREFIX)/bin/pak
	rm -vf $(DESTDIR)$(PREFIX)/bin/unpak

dist:
	mkdir -p pak-$(VERSION)
	cp $(DISTFILES) pak-$(VERSION)
	tar -cf pak-$(VERSION).tar pak-$(VERSION)
	gzip pak-$(VERSION).tar
	rm -rf pak-$(VERSION)

clean:
	rm -f pak pak.o pak-$(VERSION).tar.gz

.PHONY: all install uninstall dist clean
