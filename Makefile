CFLAGS=-g -Wall -DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED `pkg-config gtk+-2.0 --cflags` -std=c99 `pkg-config gtkglext-1.0 --cflags`
LDFLAGS=`pkg-config gtk+-2.0 --libs` `pkg-config gtkglext-1.0 --libs`
#-s

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
	rm -f config.status confige.log config.h tags .dep
	rm -fr autom4te.cache

.PHONY: install
install:
	install dsoda $(PREFIX)/bin
	install misc/dsodafw $(PREFIX)/bin
	mkdir -p $(PREFIX)/share/dsoda
	install misc/extractor.pl $(PREFIX)/share/dsoda
	install misc/dsoda_icon.png $(PREFIX)/share/dsoda
	install LICENSE $(PREFIX)/share/dsoda
	mkdir -p $(FIRMWARE_DIR)
	install misc/dso2250_loader.hex $(FIRMWARE_DIR)
	install misc/dso2250_firmware.hex $(FIRMWARE_DIR)
	mkdir -p $(PREFIX)/share/man
	install misc/dsoda.1 $(PREFIX)/share/man
	ln -s dsoda.1 $(PREFIX)/share/man/dsodafw.1
	
-include .dep
