include config.mk
CFLAGS=-g -Wall -DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED `pkg-config gtk+-2.0 --cflags` -std=c99 `pkg-config gtkglext-1.0 --cflags` -ggdb
LDFLAGS=`pkg-config gtk+-2.0 --libs` `pkg-config gtkglext-1.0 --libs`

OBJS := $(patsubst %.c,%.o,$(wildcard *.c))

all: check $(OBJS)
	gcc $(OBJS) -o dsoda $(LDFLAGS) -lusb -lpthread

.PHONY: check
check:
	if [ ! -e config.h ]; then ./configure; fi

.PHONY: dep
dep:
	$(CC) $(CCFLAGS) -MM *.c > .dep

.PHONY: clean
clean:
	rm -f $(OBJS) dsoda

.PHONY: mrproper
mrproper: clean
	rm -f config.status config.log config.h tags .dep
	rm -fr autom4te.cache

.PHONY: install
install:
	mkdir -p $(PREFIX)/bin
	install dsoda $(PREFIX)/bin
	install misc/dsodafw $(PREFIX)/bin
	mkdir -p $(PREFIX)/usr/share/dsoda
	install misc/extractor.pl $(PREFIX)/usr/share/dsoda
	install -m 644 misc/dsoda_icon.png $(PREFIX)/usr/share/dsoda
	install -m 644 LICENSE $(PREFIX)/usr/share/dsoda
	install -m 644 README $(PREFIX)/usr/share/dsoda
	mkdir -p $(PREFIX)/usr/share/man/man1
	install -m 644 misc/dsoda.1 $(PREFIX)/usr/share/man/man1
	ln -s dsoda.1 $(PREFIX)/usr/share/man/man1/dsodafw.1
	mkdir -p $(PREFIX)/lib/firmware
	install -m 644 misc/dso2250_loader.hex $(PREFIX)/lib/firmware
	install -m 644 misc/dso2250_firmware.hex $(PREFIX)/lib/firmware
	mkdir -p $(PREFIX)/etc/udev/rules.d
	install -m 644 misc/dso2250.rules $(PREFIX)/etc/udev/rules.d/20-dso2250.rules
	mkdir -p $(PREFIX)/usr/share/pixmaps
	install -m 644 misc/dsoda_icon.png $(PREFIX)/usr/share/pixmaps
	mkdir -p $(PREFIX)/usr/share/applications
	install -m 644 misc/dsoda.desktop $(PREFIX)/usr/share/applications
	
-include .dep
