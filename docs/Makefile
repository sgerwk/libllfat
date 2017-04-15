HTML=fat_lib.html fat_functions.html fat.html \
fatbackup.html fatshrink.html fattool.html fatview.html

all: ${HTML}

%.html: %.1
	mantohtml $< $@

%.html: %.3
	mantohtml $< $@

%.html: %.5
	mantohtml $< $@

clean:
	rm -f ${HTML}

