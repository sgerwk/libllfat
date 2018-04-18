PROGS=fattest fatexample fatview fatshrink fattool fatbackup
VPATH=../tools

CC=gcc
CFLAGS+=-g -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2
CFLAGS+=-I../lib
CFLAGS+=-g
LDFLAGS+=-L../lib -Wl,-rpath,'$$ORIGIN/../lib'
LDLIBS=-lllfat

all: $(PROGS)

clean:
	rm -f *.o $(PROGS) fat12 fat16 fat32 fat32long

