CC = gcc
CFLAGS = -O2 -DSVR4 -D_GNU_SOURCE -Wall -std=gnu99 -D_FORTIFY_SOURCE=1 -Wno-unused-result
VERSION = 1.0

TARGET = ttyrec ttyplay ttytime

DIST =	ttyrec.c ttyplay.c ttyrec.h io.c io.h ttytime.c\
	README Makefile ttyrec.1 ttyplay.1 ttytime.1

BIN = $(DESTDIR)/usr/bin

all: $(TARGET)

patch:
	quilt push -a
	touch $@

ttyrec: patch ttyrec.o io.o
	$(CC) $(CFLAGS) -o ttyrec ttyrec.o io.o

ttyplay: patch ttyplay.o io.o
	$(CC) $(CFLAGS) -o ttyplay ttyplay.o io.o

ttytime: patch ttytime.o io.o
	$(CC) $(CFLAGS) -o ttytime ttytime.o io.o

clean:
	rm -f *.o $(TARGET) ttyrecord *~
	quilt pop -a || true
	rm -f patch

install:
	install -d -m 0755 -o root -g root $(BIN)
	install    -m 0755 -o root -g root ttyrec $(BIN)
	install    -m 0755 -o root -g root ttyplay $(BIN)
	install    -m 0755 -o root -g root ttytime $(BIN)
	install    -m 0755 -o root -g root helper/telnetpassfile $(BIN)

dist:
	rm -rf ttyrec-$(VERSION)
	rm -f ttyrec-$(VERSION).tar.gz

	mkdir ttyrec-$(VERSION)
	cp $(DIST) ttyrec-$(VERSION)
	tar zcf ttyrec-$(VERSION).tar.gz  ttyrec-$(VERSION)
	rm -rf ttyrec-$(VERSION)
