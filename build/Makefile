PROGS=fattest fatexample fatview fatshrink fattool fatbackup
VPATH=../tools

CC=gcc
CFLAGS+=-Wall -Wextra
CFLAGS+=-I../lib
CFLAGS+=-g
LDFLAGS+=-L../lib -Wl,-rpath,'$$ORIGIN/../lib'
LDLIBS=-lllfat

all: $(PROGS)

clean:
	rm -f *.o $(PROGS) fat12 fat16 fat32 fat32long

