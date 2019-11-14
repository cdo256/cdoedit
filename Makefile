# cdoedit - simple text editor
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC = cdoedit.c x.c
OBJ = $(SRC:.c=.o)

all: options cdoedit

options:
	@echo cdoedit build options:
	@echo "CFLAGS  = $(STCFLAGS)"
	@echo "LDFLAGS = $(STLDFLAGS)"
	@echo "CC      = $(CC)"

config.h:
	cp config.def.h config.h

.c.o:
	$(CC) $(STCFLAGS) -c $<

cdoedit.o: config.h cdoedit.h win.h
x.o: arg.h config.h cdoedit.h win.h

$(OBJ): config.h config.mk

cdoedit: $(OBJ)
	$(CC) -o $@ $(OBJ) $(STLDFLAGS)

clean:
	rm -f cdoedit $(OBJ) cdoedit-$(VERSION).tar.gz

dist: clean
	mkdir -p cdoedit-$(VERSION)
	cp -R LICENSE Makefile README config.mk\
		config.def.h arg.h cdoedit.h win.h $(SRC)\
		cdoedit-$(VERSION)
	tar -cf - cdoedit-$(VERSION) | gzip > cdoedit-$(VERSION).tar.gz
	rm -rf cdoedit-$(VERSION)

install: cdoedit
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f cdoedit $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/cdoedit

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/cdoedit

.PHONY: all options clean dist install uninstall
