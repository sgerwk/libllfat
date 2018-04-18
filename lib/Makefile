LIBS=libllfat.so
HEADERS=llfat.h

CC=gcc
CFLAGS+=-g -O2 -Wall -Wextra
CFLAGS+=-I.
CFLAGS+=-fPIC
# CFLAGS+=-g

OBJS=fs.o table.o unit.o entry.o directory.o reference.o inverse.o \
long.o complex.o

all: $(LIBS) $(HEADERS)
 
libllfat.a: $(OBJS)
	ar rcs $@ $^

libllfat.so: $(OBJS)
	gcc -shared -o $@ $^ -fPIC

llfat.h: llfat.h.header fs.h entry.h table.h directory.h reference.h \
inverse.h long.h complex.h debug.h
	cat $^ > $@

clean:
	rm -f *.o $(LIBS) $(HEADERS)

