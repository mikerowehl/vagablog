#
# arch-tag: vagablog Makefile
# 
# Copyright (C) 2003, 2004, 2005, Mike Rowehl <miker@bitsplitter.net>
#

CC      = m68k-palmos-gcc
CFLAGS  = -Wall -Os -g -mdebug-labels
# CFLAGS  = -Wall -Os
OBJS    = vagablog.o http.o
LIBS    = -lNetSocket
INCLUDE =
PRCNAME = vagablog

all: $(PRCNAME).prc

$(PRCNAME).prc: vagablog bin.stamp
	build-prc $(PRCNAME).def vagablog *.bin

vagablog: $(OBJS)
	$(CC) $(OBJS) $(CFLAGS) -o vagablog $(LIBS)

vagablog.o: vagablog.c rsrc/resource.h
	$(CC) $(CFLAGS) $(INCLUDE) -c vagablog.c

%.o: %.c %.h
	$(CC) $(CFLAGS) $(INCLUDE) -c $<

bin.stamp: rsrc/$(PRCNAME).rcp
	pilrc -I rsrc/ -q rsrc/$(PRCNAME).rcp
	touch bin.stamp

clean:
	rm -f *.o *.bin vagablog rsrc/*.bin bin.stamp *.prc

