CFLAGS=-g -Wall -DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED `pkg-config gtk+-2.0 --cflags` -ggdb -std=c99 `pkg-config gtkglext-1.0 --cflags`
LDFLAGS=`pkg-config gtk+-2.0 --libs` -lgtkdatabox `pkg-config gtkglext-1.0 --libs`
#-s

OBJS := $(patsubst %.c,%.o,$(wildcard *.c))

all: $(OBJS)
	gcc $(OBJS) -o dsoda $(LDFLAGS) -lusb -lpthread

.PHONY: dep
dep:
	$(CC) $(CCFLAGS) -MM *.c > .dep

.PHONY: clean
clean:
	rm -f $(OBJS) dsoda

-include .dep
