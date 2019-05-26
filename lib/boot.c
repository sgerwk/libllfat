/*
 * boot.c
 * Copyright (C) 2016 <sgerwk@aol.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * boot.c
 *
 * access the data in a boot sector, its backup and the information sector
 */

#define _FILE_OFFSET_BITS 64
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <endian.h>
#include <search.h>
#include <ctype.h>
#include <linux/fs.h>
#include "boot.h"
#include "table.h"

int fatbootdebug = 0;
#define dprintf if (fatbootdebug) printf

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

/*
 * argument contains exactly one bit 1
 */
int _onebit(uint8_t c) {
	int n, s;

	if (sizeof(int) >= 3)
		return c != 0 && ((((c - 1) << 15) / c) & 0xFF) == 0x00;

	s = 0;
	for (n = 0; n < 8; n++)
		if (c & (1 << n))
			s++;
	return s == 1;
}

/*
 * bits of FAT (12, 16 or 32) for a given number of clusters
 */
int fatbitsfromclusters(int32_t nclusters) {
	return nclusters < 4085 ? 12 : nclusters < 65525 ? 16 : 32;
}

/*
 * bits of FAT (12, 16 or 32) from a boot sector
 */
int fatbootbits(unit *boot) {
	int32_t clusters;

	if (fatunitgetdata(boot) == NULL)
		return -1;

	if (fatbootgetbytespersector(boot) == 0 ||
	    fatbootgetsectorspercluster(boot) == 0)
		return -1;

	clusters = fatbootnumdataclusters(boot, NULL);
	dprintf("clusters: %d\n", clusters);

	return fatbitsfromclusters(clusters);
}

/*
 * bits of FAT (12, 16 or 32), possibly with a known value
 */
int _fatbootbits(unit *boot, int *bits) {
	return bits == NULL ?
		fatbootbits(boot) :
		*bits != 0 ?
			*bits :
			(*bits = fatbootbits(boot));
}

/*
 * bits of FAT, according to the signature
 * not correct according to the official spec
 */
int fatbootsignaturebits(unit *boot) {
	if (! memcmp(fatunitgetdata(boot) + 0x36, "FAT12   ", 8))
		return 12;

	if (! memcmp(fatunitgetdata(boot) + 0x36, "FAT16   ", 8))
		return 16;

	if (! memcmp(fatunitgetdata(boot) + 0x52, "FAT32   ", 8))
		return 32;

	return -1;
}

/*
 * bytes per sector
 */

int fatbootgetbytespersector(unit *boot) {
	return le16toh(_unit16int(boot, 0x0B));
}

int fatbootsetbytespersector(unit *boot, int bytes) {
	if (boot == NULL)
		return -1;
	_unit16int(boot, 0x0B) = htole16(bytes);
	boot->dirty = 1;
	return 0;
}

/*
 * sectors per cluster
 */

uint8_t fatbootgetsectorspercluster(unit *boot) {
	return _unit8int(boot, 0x0D);
}

int fatbootsetsectorspercluster(unit *boot, int sectors) {
	if (boot == NULL)
		return -1;
	if (sectors >= 256 || sectors < 0 || ! _onebit(sectors))
		return -1;
	_unit8int(boot, 0x0D) = sectors & 0xFF;
	boot->dirty = 1;
	return 0;
}

/*
 * bytes per clusters
 */
int fatbootbytespercluster(unit *boot) {
	return fatbootgetbytespersector(boot) *
	       fatbootgetsectorspercluster(boot);
}

/*
 * reserved sectors
 */
int fatbootgetreservedsectors(unit *boot) {
	return le16toh(_unit16int(boot, 0x0E));
}

int fatbootsetreservedsectors(unit *boot, int sectors) {
	if (boot == NULL)
		return -1;
	_unit16int(boot, 0x0E) = htole16(sectors);
	boot->dirty = 1;
	return 0;
}

/*
 * number of fats
 */

int fatbootgetnumfats(unit *boot) {
	return _unit8int(boot, 0x10);
}

int fatbootsetnumfats(unit *boot, int nfat) {
	if (boot == NULL)
		return -1;
	_unit8int(boot, 0x10) = nfat;
	boot->dirty = 1;
	return 0;
}

/*
 * total number of sectors in the filesystem
 */

uint32_t fatbootgetnumsectors(unit *boot, int *bits) {
	uint16_t s;
	uint32_t l;

	s = le16toh(_unit16int(boot, 0x13));
	l = le32toh(_unit32int(boot, 0x20));

	// number of bits not known and not required: use what is available;
	// this breaks the loop bits->numdataclusters->numsectors->bits
	if (bits == NULL)
		return s != 0 ? s : l;

	// number of bits unknown but required: *bits=0; _fatbootbits calls
	// fatbootbits, which calls fatbootnumdataclusters with bits=NULL,
	// which calls this function again with bits=NULL, and the previous if
	// is executed
	return _fatbootbits(boot, bits) == 32 ? l : s != 0 ? s : l;
}

int fatbootsetnumsectors(unit *boot, int *bits, uint32_t sectors) {
	uint16_t s;

	if (boot == NULL)
		return -1;
	
	s = sectors & 0xffff;

	if (_fatbootbits(boot, bits) == 12)
		if (sectors <= 0xffff)
			_unit16uint(boot, 0x13) = htole16(s);
		else
			return -1;
	else if ((_fatbootbits(boot, bits) == 16) && (sectors <= 0xffff))
		_unit16uint(boot, 0x13) = htole16(s);
	else {
		_unit16uint(boot, 0x13) = htole16(0);
		_unit32uint(boot, 0x20) = htole32(sectors);
	}

	boot->dirty = 1;
	return 0;
}

/*
 * size of each fat: 16-bit version, 32-bit version, fatbits-dependent version
 */

int fatbootgetfatsize16(unit *boot) {
	if (boot == NULL)
		return -1;
	return 0xFFFF & le16toh(_unit16int(boot, 0x16));
}

int fatbootsetfatsize16(unit *boot, int size) {
	if (boot == NULL)
		return -1;
	_unit16int(boot, 0x16) = htole16(size);
	return 0;
}

int fatbootgetfatsize32(unit *boot) {
	if (boot == NULL)
		return -1;
	return le32toh(_unit32int(boot, 0x24));
}

int fatbootsetfatsize32(unit *boot, int size) {
	if (boot == NULL)
		return -1;
	_unit32int(boot, 0x24) = htole32(size);
	return 0;
}

int fatbootgetfatsize(unit *boot, int *bits) {
	int s;
	if (boot == NULL)
		return -1;

	// number of bits not known and not required: use what available;
	// this also breaks the loop bits->numdataclusters->fatsize->bits
	if (bits == NULL) {
		s = fatbootgetfatsize16(boot);
		return s != 0 ? s : fatbootgetfatsize32(boot);
	}

	// number of bits unknown but required: *bits==0, and _fatbootbits
	// calls fatbootbits, which calls fatbootnumdataclusters with
	// bits=NULL, which calls this function again with bits=NULL, and the
	// previous if is executed
	return _fatbootbits(boot, bits) == 32 ?
		fatbootgetfatsize32(boot) : fatbootgetfatsize16(boot);
}

int fatbootsetfatsize(unit *boot, int *bits, int size) {
	if (boot == NULL)
		return -1;
	if (_fatbootbits(boot, bits) == 32) {
		fatbootsetfatsize16(boot, 0);
		fatbootsetfatsize32(boot, size);
	}
	else {
		fatbootsetfatsize16(boot, size);
		fatbootsetfatsize32(boot, 0);
	}
	boot->dirty = 1;
	return 0;
}

/*
 * check consistency of sizes in a filesystem
 */
int fatbootconsistentsize(unit *boot, int *bits) {
	uint32_t sectors;

	sectors = fatbootgetreservedsectors(boot);
	sectors += fatbootgetfatsize(boot, bits) * fatbootgetnumfats(boot);
	sectors += fatbootgetrootentries(boot, bits) * 32 /
			fatbootgetbytespersector(boot);
	sectors += fatbootgetsectorspercluster(boot) *
			(_fatbootbits(boot, bits) == 32 ? 2 : 1);

	return sectors <= fatbootgetnumsectors(boot, bits);
}

/*
 * first cluster of root directory
 */

int32_t fatbootgetrootbegin(unit *boot, int *bits) {
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16)
		return FAT_ROOT;
	else if (_fatbootbits(boot, bits) == 32)
		return le32toh(_unit32int(boot, 0x2C));
	else
		return FAT_ERR;
}

int fatbootsetrootbegin(unit *boot, int *bits, int32_t num) {
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16)
		return -1;
	else if (_fatbootbits(boot, bits) == 32) {
		_unit32int(boot, 0x2C) = htole32(num);
		boot->dirty = 1;
		return 0;
	}
	else
		return -1;
}

/*
 * max number of entries in the root directory, zero for FAT32 (no limit)
 */

int fatbootgetrootentries(unit *boot, int *bits) {
	// bits of FAT not known and not required: use what available;
	// this breaks the loop bits->numdatacluster->rootentries->bits
	if (bits == NULL)
		return le16toh(_unit16int(boot, 0x11));

	// bits of FAT unknown but required: *bits=0, _fatbootbits calls
	// fatgetnumdataclusters with bits=NULL, which calls this function
	// again with bits=NULL and the previous if is executed
	return _fatbootbits(boot, bits) == 32 ?
		0 : le16toh(_unit16int(boot, 0x11));
}

int fatbootsetrootentries(unit *boot, int *bits, int entries) {
	if (boot == NULL)
		return -1;

	if ((entries * 32) % fatbootbytespercluster(boot) != 0)
		return -2;

	if (_fatbootbits(boot, bits) == 32)
		_unit16int(boot, 0x11) = htole16(0);
	else
		_unit16int(boot, 0x11) = htole16(entries);
	boot->dirty = 1;
	return 0;
}

/*
 * number of sectors for the root directory
 */
int32_t fatbootnumrootsectors(unit *boot, int *bits) {
	return (fatbootgetrootentries(boot, bits) * 32 +
		(fatbootgetbytespersector(boot) - 1)) /
		fatbootgetbytespersector(boot);
}

/*
 * total number of clusters in filesystem
 */

int32_t _fatbootnumclusters(unit *boot, int *bits) {
	return (fatbootgetnumsectors(boot, bits) -
		fatbootgetreservedsectors(boot) -
		fatbootgetnumfats(boot) * fatbootgetfatsize(boot, bits)) /
			fatbootgetsectorspercluster(boot);
}

/*
 * number of data clusters (first cluster is 2)
 */

#define CONDITIONALMINUS(a, b)			\
	if ((a) < (uint32_t) (b)) {		\
		dprintf("%u < %d\n", a, b);	\
		return -1;			\
	}					\
	a -= b;

int32_t fatbootnumdataclusters(unit *boot, int *bits) {
	uint32_t nsectors;
	nsectors = fatbootgetnumsectors(boot, bits);
	CONDITIONALMINUS(nsectors, fatbootgetreservedsectors(boot));
	CONDITIONALMINUS(nsectors,
		fatbootgetnumfats(boot) * fatbootgetfatsize(boot, bits));
	CONDITIONALMINUS(nsectors, fatbootnumrootsectors(boot, bits));
	return nsectors / fatbootgetsectorspercluster(boot);
}

/*
 * last cluster
 */
int32_t fatbootlastcluster(unit *boot, int *bits) {
	return 2 + fatbootnumdataclusters(boot, bits) - 1;
}

/*
 * check whether a cluster number is valid
 */
int fatbootisvalidcluster(unit *boot, int *bits, int32_t n) {
	return n >= FAT_FIRST && n <= fatbootlastcluster(boot, bits);
}

/*
 * minimal size of a fat for a given number of clusters
 */
int fatbootminfatsize(unit *boot, int32_t nclusters) {
	long long int nentries, nbytes, nsectors;
	int bits;

	bits = fatbootbits(boot);

	nentries = 2LLU + nclusters;
	nbytes = nentries * bits / 8
		+ (bits == 12 && nentries % 2 ? 1 : 0);
	nsectors = (nbytes + fatbootgetbytespersector(boot) - 1) /
		fatbootgetbytespersector(boot);

	return nsectors;
}

/*
 * set the size of each fat to its minimum possible value
 */
int fatbootbestfatsize(unit *boot) {
	int best, fatsize[3];
	int32_t nclusters[3];
	int i, bits;

	bits = 12;	// force storing 1 in the boot sector
	fatbootsetfatsize(boot, &bits, 1);

	for (i = 0; i < 3; i++) {
		nclusters[i] = -2 - i;
		fatsize[i] = -2 - i;
	}

	dprintf("fat bits: %d\n", fatbootbits(boot));

	for (i = 0;
	     nclusters[(i + 2) % 3] != nclusters[(i + 0) % 3] ||
	     fatsize[(i + 2) % 3] != fatsize[(i + 0) % 3];
	     i = (i + 1) % 3) {
		nclusters[i] = fatbootnumdataclusters(boot, NULL);
		fatsize[i] = fatbootminfatsize(boot,
			MIN((uint32_t) nclusters[i], 0x7FFFFFFF));
		dprintf("clusters: %u ", nclusters[i]);
		dprintf("fatsize: %u\n", fatsize[i]);
		fatbootsetfatsize(boot, NULL, fatsize[i]);
		if (fatbootgetfatsize(boot, NULL) < fatsize[i])
			fatbootsetfatsize(boot, NULL, 0xFFFF);
	}

	best = fatsize[(i + 2) % 3] > fatsize[(i + 0) % 3] ?
		fatsize[(i + 2) % 3] :
		fatsize[(i + 0) % 3];
	fatbootsetfatsize(boot, NULL, best);
	dprintf("best: %d\n", best);
	return best;
}

/*
 * dirty bits
 */

int fatbootgetdirtybits(unit *boot, int *bits) {
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16)
		return _unit8uint(boot, 0x25) & (FAT_UNCLEAN | FAT_IOERROR);
	else if (_fatbootbits(boot, bits) == 32)
		return _unit8uint(boot, 0x41) & (FAT_UNCLEAN | FAT_IOERROR);
	else return -1;
}

int fatbootsetdirtybits(unit *boot, int *bits, int dirty) {
	dirty &= FAT_UNCLEAN | FAT_IOERROR;
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16) {
		_unit8uint(boot, 0x25) &= ~ (FAT_UNCLEAN | FAT_IOERROR);
		_unit8uint(boot, 0x25) |= dirty;
		boot->dirty = 1;
		return 0;
	}
	else if (_fatbootbits(boot, bits) == 32) {
		_unit8uint(boot, 0x41) &= ~ (FAT_UNCLEAN | FAT_IOERROR);
		_unit8uint(boot, 0x41) |= dirty;
		boot->dirty = 1;
		return 0;
	}
	return -1;
}

/*
 * extended boot signature
 */

int fatbootgetextendedbootsignature(unit *boot, int *bits) {
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16)
		return _unit8uint(boot, 0x26) == 0x29;
	else if (_fatbootbits(boot, bits) == 32)
		return _unit8uint(boot, 0x42) == 0x29;
	else
		return FAT_ERR;
}
int fatbootaddextendedbootsignature(unit *boot, int *bits) {
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16)
		_unit8uint(boot, 0x26) = 0x29;
	else if (_fatbootbits(boot, bits) == 32)
		_unit8uint(boot, 0x42) = 0x29;
	else
		return FAT_ERR;
	return 0;
}
int fatbootdeleteextendedbootsignature(unit *boot, int *bits) {
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16)
		_unit8uint(boot, 0x26) = 0x0;
	else if (_fatbootbits(boot, bits) == 32)
		_unit8uint(boot, 0x42) = 0x0;
	else
		return FAT_ERR;
	return 0;
}

uint32_t fatbootgetserialnumber(unit *boot, int *bits) {
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16)
		return _unit32uint(boot, 0x27);
	else if (_fatbootbits(boot, bits) == 32)
		return _unit32uint(boot, 0x43);
	else
		return FAT_ERR;
}
int fatbootsetserialnumber(unit *boot, int *bits, uint32_t serial) {
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16) {
		_unit32uint(boot, 0x27) = serial;
		boot->dirty = 1;
	}
	else if (_fatbootbits(boot, bits) == 32) {
		_unit32uint(boot, 0x43) = serial;
		boot->dirty = 1;
	}
	else
		return FAT_ERR;
	return 0;
}

char* fatbootgetvolumelabel(unit *boot, int *bits) {
	char *l = malloc(12);
	l[11] = '\0';
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16)
		memcpy(l, fatunitgetdata(boot) + 0x2b, 11);
	else if (_fatbootbits(boot, bits) == 32)
		memcpy(l, fatunitgetdata(boot) + 0x47, 11);
	else {
		free(l);
		return NULL;
	}
	return l;
}
int fatbootsetvolumelabel(unit *boot, int *bits, const char l[11]) {
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16) {
		memcpy(fatunitgetdata(boot) + 0x2b, l, 11);
		boot->dirty = 1;
	}
	else if (_fatbootbits(boot, bits) == 32) {
		memcpy(fatunitgetdata(boot) + 0x47, l, 11);
		boot->dirty = 1;
	}
	else
		return -1;
	return 0;
}

char* fatbootgetfilesystemtype(unit *boot, int *bits) {
	char *t = malloc(9);
	t[8] = '\0';
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16)
		memcpy(t, fatunitgetdata(boot) + 0x36, 8);
	else if (_fatbootbits(boot, bits) == 32)
		memcpy(t, fatunitgetdata(boot) + 0x52, 8);
	else {
		free(t);
		return NULL;
	}
	return t;
}
int fatbootsetfilesystemtype(unit *boot, int *bits, char t[8]) {
	if (_fatbootbits(boot, bits) == 12 || _fatbootbits(boot, bits) == 16) {
		memcpy(fatunitgetdata(boot) + 0x36, t, 8);
		boot->dirty = 1;
	}
	else if (_fatbootbits(boot, bits) == 32) {
		memcpy(fatunitgetdata(boot) + 0x52, t, 8);
		boot->dirty = 1;
	}
	else
		return -1;
	return 0;
}

/*
 * backup sector
 */

int fatbootgetbackupsector(unit *boot) {
	int b;
	b = le16toh(_unit16int(boot, 0x32));
	if (b == 0x0000 || b == 0xFFFF)
		return -1;
	return b;
}

int fatbootsetbackupsector(unit *boot, int sector) {
	if (boot == NULL)
		return -1;

	if (sector == 0 || sector > fatbootgetreservedsectors(boot) ||
	    (sector == fatbootgetinfopos(boot)))
		return -1;

	_unit16int(boot, 0x32) = htole16(sector);
	boot->dirty = 1;
	return 0;
}

/*
 * position of information sector, if any
 */

int fatbootgetinfopos(unit *boot) {
	return le16toh(_unit16int(boot, 0x30));
}

int fatbootsetinfopos(unit *boot, int pos) {
	_unit16int(boot, 0x30) = htole16(pos);
	boot->dirty = 1;
	return 0;
}

/*
 * media descriptor, mainly used for checking if a file is a fat filesystem
 */

uint8_t fatbootgetmedia(unit *boot) {
	return _unit8int(boot, 0x15);
}

int fatbootsetmedia(unit *boot, uint8_t media) {
	_unit8int(boot, 0x15) = media;
	boot->dirty = 1;
	return 0;
}

/*
 * boot sector signature
 */

int _fatbootsignaturepos(unit *boot) {
	int b;
	b = fatbootgetbytespersector(boot);
	return (b < 512 ? b : 512) - 2;
}

int fatbootgetbootsignature(unit *boot) {
	uint16_t sig;
	int pos;

	if (boot == NULL) {
		printf("boot sector not loaded\n");
		exit(1);
	}

	pos = _fatbootsignaturepos(boot);
	sig = le16toh(_unit16uint(boot, pos));

	dprintf("0x%04X\n", sig);
	return sig == 0xAA55;
}

int fatbootsetbootsignature(unit *boot) {
	if (boot == NULL)
		return -1;
	_unit16uint(boot, _fatbootsignaturepos(boot)) = htole16(0xAA55);
	boot->dirty = 1;
	return 0;
}

/*
 * check apparent validity of a boot sector
 */
int fatbootcheck(unit *boot) {
	int res = 0;
	int bits = 0;

	if (! _onebit(fatbootgetsectorspercluster(boot)))
		res--;

	if (fatbootgetreservedsectors(boot) == 0)
		res--;

	if (fatbootgetnumfats(boot) == 0)
		res--;

	if (fatbootgetmedia(boot) < 0xE5)	// not exact
		res--;

	if (fatbootgetbytespersector(boot) != 0 &&
	    fatbootgetsectorspercluster(boot) != 0 &&
	    fatbootlastcluster(boot, &bits) < FAT_FIRST)
		res--;

	if (fatbootgetbytespersector(boot) != 0 &&
	    fatbootgetsectorspercluster(boot) != 0 &&
	    _fatbootbits(boot, &bits) != 0 &&
	    1LLU * fatbootgetfatsize(boot, &bits) *
		fatbootgetbytespersector(boot) * 8 / bits
			< (unsigned) fatbootlastcluster(boot, &bits) + 1)
		res--;

	if (0 && fatbootbytespercluster(boot) > 32 * 1024)
		// not really mandatory
		res--;

	if (fatbootgetreservedsectors(boot) < 1)
		res--;

	if (fatbootgetreservedsectors(boot) <= fatbootgetinfopos(boot) &&
	    fatbootgetinfopos(boot) != 0x0000 &&
	    fatbootgetinfopos(boot) != 0xFFFF &&
	    bits == 32)
		res--;

	if (_fatbootbits(boot, &bits) == 32 &&
	    fatbootgetbackupsector(boot) >= fatbootgetreservedsectors(boot))
		res--;

	if (fatbootgetbytespersector(boot) != 0 &&
	    (fatbootgetrootentries(boot, &bits) * 32) %
		fatbootgetbytespersector(boot) != 0)
		res--;

	if (0)			// TBD: first two entries in fat
		res--;

	if (! memcmp(fatunitgetdata(boot) + 0x03, "EXFAT   ", 8))
		res--;

	if (fatbootgetbytespersector(boot) &&
	    fatbootgetsectorspercluster(boot) != 0 &&
	    _fatbootbits(boot, &bits) == 32 &&
	    ! fatbootisvalidcluster(boot, &bits,
	    	fatbootgetrootbegin(boot, &bits)))
		res--;

	return res;
}

/*
 * information sector signatures
 */

int fatinfogetinfosignatures(unit *info) {
	uint32_t first, second;
	uint16_t third;

	if (info == NULL)
		return -1;

	first =  le32toh(_unit32uint(info, 0x000));
	second = le32toh(_unit32uint(info, 0x1E4));
	third =  le16toh(_unit16uint(info, 0x1FE));

	dprintf("0x%08X\n", first);
	dprintf("0x%08X\n", second);
	dprintf("0x%04X\n", third);

	return first == 0x41615252 && second == 0x61417272 && third == 0xAA55;
}

int fatinfosetinfosignatures(unit *info) {
	if (info == NULL)
		return -1;

	_unit32uint(info, 0x000) = htole32(0x41615252);
	_unit32uint(info, 0x1E4) = htole32(0x61417272);
	_unit16uint(info, 0x1FE) = htole16(0xAA55);

	info->dirty = 1;

	return 0;
}

/*
 * last allocated cluster
 */

int32_t fatinfogetlastallocatedcluster(unit *info) {
	if (info == NULL)
		return -1;
	return le32toh(_unit32int(info, 0x1EC));
}

void fatinfosetlastallocatedcluster(unit *info, int32_t last) {
	if (info == NULL)
		return;

	_unit32int(info, 0x1EC) = htole32(last);
	info->dirty = 1;
}

/*
 * number of free clusters
 */

int32_t fatinfogetfreeclusters(unit *info) {
	if (info == NULL)
		return -1;
	return le32toh(_unit32int(info, 0x1E8));
}

void fatinfosetfreeclusters(unit *info, int32_t last) {
	if (info == NULL)
		return;
	_unit32int(info, 0x1E8) = htole32(last);
	info->dirty = 1;
}

