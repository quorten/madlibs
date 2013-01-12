X = .exe
prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
datadir = $(prefix)/share

VERSION = 0.1

madlibs_SOURCES = \
	madlibs.c \
	bool.h exparray.h xmalloc.c xmalloc.h

DISTFILES = $(madlibs_SOURCES) \
	README ChangeLog Libraries.txt Makefile exparray.gdb \
	classic.mlb extended.mlb

all: madlibs$(X)

madlibs$(X): madlibs.c
	gcc -g -o $@ madlibs.c xmalloc.c

clean:
	rm -f madlibs$(X)

install:
	install madlibs$(X) $(bindir)/madlibs$(X)

uninstall:
	rm -f $(bindir)/madlibs$(X)

dist:
	mkdir madlibs-$(VERSION)
	cp $(DISTFILES) madlibs-$(VERSION)
	zip -9rq madlibs-$(VERSION).zip madlibs-$(VERSION)
	rm -rf madlibs-$(VERSION)
