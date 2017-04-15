all:
	+make -C lib
	+make -C build

install: all
	mkdir -p $(DESTDIR)/usr/include
	cp lib/llfat.h $(DESTDIR)/usr/include
	mkdir -p $(DESTDIR)/usr/lib
	cp lib/libllfat.so $(DESTDIR)/usr/lib
	mkdir -p $(DESTDIR)/usr/bin
	install build/fattool build/fatshrink $(DESTDIR)/usr/bin
	install build/fatview build/fatbackup $(DESTDIR)/usr/bin
	mkdir -p $(DESTDIR)/usr/share/man/man1
	cp docs/fattool.1 docs/fatshrink.1 $(DESTDIR)/usr/share/man/man1
	cp docs/fatview.1 docs/fatbackup.1 $(DESTDIR)/usr/share/man/man1
	mkdir -p $(DESTDIR)/usr/share/man/man3
	cp docs/fat_lib.3 docs/fat_functions.3 $(DESTDIR)/usr/share/man/man3
	mkdir -p $(DESTDIR)/usr/share/man/man5
	cp docs/fat.5 $(DESTDIR)/usr/share/man/man5
	mkdir -p $(DESTDIR)/usr/share/docs/packages/libllfat
	cp DISCLAIMER COPYING $(DESTDIR)/usr/share/docs/packages/libllfat
	cp docs/libllfat.txt $(DESTDIR)/usr/share/docs/packages/libllfat
	cp tools/fatexample.c $(DESTDIR)/usr/share/docs/packages/libllfat

clean:
	make -C lib clean
	make -C build clean

