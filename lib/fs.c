/*
 * fs.c
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
#include "fs.h"
#include "table.h"

int fatdebug = 0;
#define dprintf if (fatdebug) printf

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

#ifdef O_DIRECT
#else
#define O_DIRECT 0
#endif

/*
 * sector size, used only to read the boot sector
 * that sector contains the size of the other sectors
 */
#define BOOTSECTORSIZE 512

/*
 * init a fat structure
 */
fat *fatcreate() {
	fat *f;

	f = malloc(sizeof(fat));

	f->fd = -1;
	f->devicename = NULL;
	f->bits = 0;
	f->nfat = FAT_ALL;
	f->insensitive = 0;

	f->boot = NULL;
	f->info = NULL;
	f->sectors = NULL;
	f->clusters = NULL;

	f->last = 2;
	f->free = -1;
	f->user = NULL;

	return f;
}

/*
 * open a fat (also read the boot sector)
 */
fat *_fatopen(char *filename, off_t offset, int (*fatbits)(fat *f)) {
	fat *f;
	int32_t info;

	f = fatcreate();
	f->devicename = filename;

	f->fd = open(filename, O_RDWR | O_DIRECT);
	if (f->fd == -1) {
		perror(filename);
		free(f);
		return NULL;
	}
	f->offset = offset;

	f->boot = fatunitget(&f->sectors, f->offset, BOOTSECTORSIZE, 0, f->fd);
	if (f->boot == NULL) {
		printf("error reading boot sector\n");
		return NULL;
	}
	f->boot->refer = 1;

	f->bits = fatbits(f);

	if (fatbits(f) == -1) {
		printf("cannot determine bits of FAT\n");
		return NULL;
	}

	if (fatbits(f) != 32)
		return f;

	info = fatgetinfopos(f);
	if (info == 0 || info == 0xFFFF)
		return 0;

	f->info = fatunitget(&f->sectors,
		f->offset, fatgetbytespersector(f), info, f->fd);
	if (f->info == NULL)
		return 0;

	f->info->refer = 1;
	if (! fatgetinfosignatures(f))
		return 0;

	f->last = fatgetlastallocatedcluster(f);
	if (! fatisvalidcluster(f, f->last))
		f->last = 2;

	f->free = fatgetfreeclusters(f);
	if (f->free < 0 || f->free > fatnumdataclusters(f))
		f->free = -1;

	return f;
}
fat *fatopen(char *filename, off_t offset) {
	return _fatopen(filename, offset, fatbits);
}
fat *fatsignatureopen(char *filename, off_t offset) {
	return _fatopen(filename, offset, fatsignaturebits);
}

/*
 * check apparent validity of a filesystem
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

long _sizeofdevice(int fd) {
	struct stat ss;
	// long size;

	if (fstat(fd, &ss))
		return -1;
	if (S_ISBLK(ss.st_mode)) {
		// any portable way to check size of device?
		// if (ioctl, BLKGETSIZE64, &size) return -1; return size;
		return -1;
	}
	return ss.st_size;
}

int fatcheck(fat *f) {
	int res = 0;

	if (! _onebit(fatgetsectorspercluster(f)))
		res--;

	if (fatgetreservedsectors(f) == 0)
		res--;

	if (fatgetnumfats(f) == 0)
		res--;

	if (fatgetmedia(f) < 0xE5)	// not exact
		res--;

	if (fatgetbytespersector(f) != 0 && fatgetsectorspercluster(f) != 0 &&
	    fatlastcluster(f) < FAT_FIRST)
		res--;
	
	if (fatgetbytespersector(f) != 0 && fatgetsectorspercluster(f) != 0 &&
	    fatbits(f) != 0 &&
	    fatgetfatsize(f) * fatgetbytespersector(f) * 8 / fatbits(f) <
			fatlastcluster(f) + 1)
		res--;

	if (0 && fatbytespercluster(f) > 32 * 1024)	// not really mandatory
		res--;

	if (fatgetreservedsectors(f) < 1)
		res--;

	if (fatgetreservedsectors(f) <= fatgetinfopos(f) &&
	    fatgetinfopos(f) != 0x0000 &&
	    fatgetinfopos(f) != 0xFFFF &&
	    fatbits(f) == 32)
		res--;

	if (0)	// TBD: enough reserved sectors for backup of boot sectors
		res--;

	if (fatgetbytespersector(f) != 0 &&
	    (fatgetrootentries(f) * 32) % fatgetbytespersector(f) != 0)
		res--;

	if (0)			// TBD: first two entries in fat
		res--;

	if (0)			// TBD: string at 0x03 is not "EXFAT   "
		res--;

				// no short images
	dprintf("size: %ld\n", _sizeofdevice(f->fd));
	if (0 && _sizeofdevice(f->fd) > 0 &&
	    fatgetnumsectors(f) >
	    (unsigned long)_sizeofdevice(f->fd) / fatgetbytespersector(f))
		res--;

	if (fatgetbytespersector(f) && fatgetsectorspercluster(f) != 0 &&
	    fatbits(f) == 32 && ! fatisvalidcluster(f, fatgetrootbegin(f)))
		res--;

	return res;
}

/*
 * flush to the filesystem
 */
int fatflush(fat *f) {
	/* boot and info sectors are also in the cache */
	fatunitflush(f->sectors);
	fatunitflush(f->clusters);
	return 0;
}

/*
 * close a fat without saving it to file
 */
int fatquit(fat *f) {
	dprintf("deallocating sectors\n");
	fatunitdeallocate(f->sectors);
	dprintf("deallocating clusters\n");
	fatunitdeallocate(f->clusters);

	if (-1 == close(f->fd)) {
		perror("closing");
		return -1;
	}
	free(f);
	return 0;
}

/*
 * close the file
 */
int fatclose(fat *f) {
	if (f->info != NULL && fatgetinfosignatures(f)) {
		dprintf("updating information sector\n");
		if (fatisvalidcluster(f, f->last))
			fatsetlastallocatedcluster(f, f->last);
		if (f->free <= fatnumdataclusters(f))
			fatsetfreeclusters(f, f->free);
	}

	if (fatflush(f))
		return -1;

	return fatquit(f);
}

/*
 * bits of fat for a given number of clusters
 */
int fatbitsfromclusters(int32_t nclusters) {
	return nclusters < 4085 ? 12 : nclusters < 65525 ? 16 : 32;
}

/*
 * determine bits of FAT: 12, 16 or 32
 */
int fatbits(fat *f) {
	int32_t clusters;

	if (f->bits != 0)
		return f->bits;

	if (fatunitgetdata(f->boot) == NULL)
		return -1;

	if (fatgetbytespersector(f) == 0 || fatgetsectorspercluster(f) == 0)
		return -1;

	clusters = fatnumdataclusters(f);
	dprintf("clusters: %d\n", clusters);

	f->bits = fatbitsfromclusters(clusters);
	return f->bits;
}

/*
 * bits of FAT, according to the signature
 * not right according to the official spec
 */
int fatsignaturebits(fat *f) {
	if (! memcmp(fatunitgetdata(f->boot) + 0x36, "FAT12   ", 8))
		return 12;

	if (! memcmp(fatunitgetdata(f->boot) + 0x36, "FAT16   ", 8))
		return 16;

	if (! memcmp(fatunitgetdata(f->boot) + 0x52, "FAT32   ", 8))
		return 32;

	return -1;
}

/*
 * bytes per sector
 */
int fatgetbytespersector(fat *f) {
	return le16toh(_unit16int(f->boot, 0x0B));
}

int fatsetbytespersector(fat *f, int bytes) {
	if (f == NULL || f->boot == NULL)
		return -1;
	_unit16int(f->boot, 0x0B) = htole16(bytes);
	f->boot->dirty = 1;
	return 0;
}

/*
 * sectors per cluster
 */
uint8_t fatgetsectorspercluster(fat *f) {
	return _unit8int(f->boot, 0x0D);
}

int fatsetsectorspercluster(fat *f, int sectors) {
	if (f == NULL || f->boot == NULL)
		return -1;
	if (sectors >= 256 || sectors < 0 || ! _onebit(sectors))
		return -1;
	_unit8int(f->boot, 0x0D) = sectors & 0xFF;
	f->boot->dirty = 1;
	return 0;
}

/*
 * bytes per clusters
 */
int fatbytespercluster(fat *f) {
	return fatgetbytespersector(f) * fatgetsectorspercluster(f);
}

/*
 * reserved sectors
 */
int fatgetreservedsectors(fat *f) {
	return le16toh(_unit16int(f->boot, 0x0E));
}

int fatsetreservedsectors(fat *f, int sectors) {
	if (f == NULL || f->boot == NULL)
		return -1;
	_unit16int(f->boot, 0x0E) = htole16(sectors);
	f->boot->dirty = 1;
	return 0;
}

/*
 * number of fats
 */

int fatgetnumfats(fat *f) {
	return _unit8int(f->boot, 0x10);
}

int fatsetnumfats(fat *f, int nfat) {
	if (f == NULL || f->boot == NULL)
		return -1;
	_unit8int(f->boot, 0x10) = nfat;
	f->boot->dirty = 1;
	return 0;
}

/*
 * total number of sectors in the filesystem
 */

uint32_t fatgetnumsectors(fat *f) {
	uint16_t s;
	uint32_t l;

	s = le16toh(_unit16int(f->boot, 0x13));
	l = le32toh(_unit32int(f->boot, 0x20));

	return (s != 0) ? s : l;
}

int fatsetnumsectors(fat *f, uint32_t sectors) {
	uint16_t s = sectors & 0xffff;

	if (f == NULL || f->boot == NULL)
		return -1;

	if (fatbits(f) == 12)
		if (sectors <= 0xffff)
			_unit16uint(f->boot, 0x13) = htole16(s);
		else
			return 1;
	else if ((fatbits(f) == 16) && (sectors <= 0xffff))
		_unit16uint(f->boot, 0x13) = htole16(s);
	else {
		_unit16uint(f->boot, 0x13) = htole16(0);
		_unit32uint(f->boot, 0x20) = htole32(sectors);
	}

	f->boot->dirty = 1;
	return 0;
}

/*
 * size of one fat: 16-bit version, 32-bit version, fatbits-dependent version
 */

int fatgetfatsize16(fat *f) {
	if (f == NULL || f->boot == NULL)
		return -1;
	return 0xFFFF & le16toh(_unit16int(f->boot, 0x16));
}

int fatsetfatsize16(fat *f, int size) {
	if (f == NULL || f->boot == NULL)
		return -1;
	_unit16int(f->boot, 0x16) = htole16(size);
	return 0;
}

int fatgetfatsize32(fat *f) {
	if (f == NULL || f->boot == NULL)
		return -1;
	return le32toh(_unit32int(f->boot, 0x24));
}

int fatsetfatsize32(fat *f, int size) {
	if (f == NULL || f->boot == NULL)
		return -1;
	_unit32int(f->boot, 0x24) = htole32(size);
	return 0;
}

int fatgetfatsize(fat *f) {
	int s;
	if (f == NULL || f->boot == NULL)
		return -1;
	if (f->bits == 0) {
		s = fatgetfatsize16(f);
		return s != 0 ? s : fatgetfatsize32(f);
	}
	else if (fatbits(f) == 32)
		return fatgetfatsize32(f);
	else
		return fatgetfatsize16(f);
}

int fatsetfatsize(fat *f, int size) {
	if (f == NULL || f->boot == NULL)
		return -1;
	if (fatbits(f) == 32) {
		fatsetfatsize16(f, 0);
		fatsetfatsize32(f, size);
	}
	else {
		fatsetfatsize16(f, size);
		fatsetfatsize32(f, 0);
	}
	f->boot->dirty = 1;
	return 0;
}

/*
 * minimal size of a fat for a given number of clusters
 */
int fatminfatsize(fat *f, int32_t nclusters) {
	long long int nentries, nbytes, nsectors;

	nentries = 2LLU + nclusters;
	nbytes = nentries * fatbits(f) / 8
		+ (fatbits(f) == 12 && nentries % 2 ? 1 : 0);
	nsectors = (nbytes + fatgetbytespersector(f) - 1) /
		fatgetbytespersector(f);

	return nsectors;
}

/*
 * set the size of a fat to its minimum possible value
 */
int fatbestfatsize(fat *f) {
	int best, fatsize[3];
	int32_t nclusters[3];
	int i;

	fatsetfatsize(f, 1);

	for (i = 0; i < 3; i++) {
		nclusters[i] = -2 - i;
		fatsize[i] = -2 - i;
	}

	dprintf("fat bits: %d\n", fatbits(f));

	for (i = 0;
	     nclusters[(i + 2) % 3] != nclusters[(i + 0) % 3] ||
	     fatsize[(i + 2) % 3] != fatsize[(i + 0) % 3];
	     i = (i + 1) % 3) {
		nclusters[i] = fatnumdataclusters(f);
		fatsize[i] = fatminfatsize(f,
			MIN((uint32_t) nclusters[i], 0x7FFFFFFF));
		dprintf("clusters: %u ", nclusters[i]);
		dprintf("fatsize: %u\n", fatsize[i]);
		fatsetfatsize(f, fatsize[i]);
		if (fatgetfatsize(f) < fatsize[i])
			fatsetfatsize(f, 0xFFFF);
	}

	best = fatsize[(i + 2) % 3] > fatsize[(i + 0) % 3] ?
		fatsize[(i + 2) % 3] :
		fatsize[(i + 0) % 3];
	fatsetfatsize(f, best);
	return best;
}

/*
 * check consistency of sizes in a filesystem (see fat_functions.3)
 */
int fatconsistentsize(fat *f) {
	return (uint32_t) fatgetreservedsectors(f) +
		fatgetfatsize(f) * fatgetnumfats(f) +
		fatgetrootentries(f) * 32 / fatgetbytespersector(f) +
		fatgetsectorspercluster(f) * (fatbits(f) == 32 ? 2 : 1)
			> fatgetnumsectors(f);
}

/*
 * first cluster of root directory
 */

int32_t fatgetrootbegin(fat *f) {
	if (fatbits(f) == 12 || fatbits(f) == 16)
		return FAT_ROOT;
	else if (fatbits(f) == 32)
		return le32toh(_unit32int(f->boot, 0x2C));
	else
		return FAT_ERR;
}

int fatsetrootbegin(fat *f, int32_t num) {
	if (fatbits(f) == 12 || fatbits(f) == 16)
		return -1;
	else if (fatbits(f) == 32) {
		_unit32int(f->boot, 0x2C) = htole32(num);
		f->boot->dirty = 1;
		return 0;
	}
	else
		return -1;
}

/*
 * max number of entries in the root directory, zero for FAT32 (no limit)
 */

int fatgetrootentries(fat *f) {
	return le16toh(_unit16int(f->boot, 0x11));
}

int fatsetrootentries(fat *f, int entries) {
	if (f == NULL || f->boot == NULL)
		return -1;

	if ((entries * 32) % fatbytespercluster(f) != 0)
		return -2;

	if (fatbits(f) == 32)
		_unit16int(f->boot, 0x11) = htole16(0);
	else
		_unit16int(f->boot, 0x11) = htole16(entries);
	f->boot->dirty = 1;
	return 0;
}

/*
 * number of sectors for the root directory
 */

int32_t fatnumrootsectors(fat *f) {
	return (fatgetrootentries(f) * 32 + (fatgetbytespersector(f) - 1)) /
		fatgetbytespersector(f);
}

/*
 * total number of clusters in filesystem
 */

int32_t _fatnumclusters(fat *f) {
	return (fatgetnumsectors(f) - fatgetreservedsectors(f) -
		fatgetnumfats(f) * fatgetfatsize(f)) /
		fatgetsectorspercluster(f);
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

int32_t fatnumdataclusters(fat *f) {
	uint32_t nsectors;
	nsectors = fatgetnumsectors(f);
	CONDITIONALMINUS(nsectors, fatgetreservedsectors(f));
	CONDITIONALMINUS(nsectors, fatgetnumfats(f) * fatgetfatsize(f));
	CONDITIONALMINUS(nsectors, fatnumrootsectors(f));
	return nsectors / fatgetsectorspercluster(f);
}

/*
 * last cluster
 */

int32_t fatlastcluster(fat *f) {
	return 2 + fatnumdataclusters(f) - 1;
}

/*
 * check if a cluster number is valid
 */

int fatisvalidcluster(fat *f, int32_t n) {
	return n >= FAT_FIRST && n <= fatlastcluster(f);
}

/*
 * dirty bits
 */

int fatgetdirtybits(fat *f) {
	if (fatbits(f) == 12 || fatbits(f) == 16)
		return _unit8uint(f->boot, 0x25) & (FAT_UNCLEAN | FAT_IOERROR);
	else if (fatbits(f) == 32)
		return _unit8uint(f->boot, 0x41) & (FAT_UNCLEAN | FAT_IOERROR);
	else return -1;
}

int fatsetdirtybits(fat *f, int dirty) {
	dirty &= FAT_UNCLEAN | FAT_IOERROR;
	if (fatbits(f) == 12 || fatbits(f) == 16) {
		_unit8uint(f->boot, 0x25) &= ~ (FAT_UNCLEAN | FAT_IOERROR);
		_unit8uint(f->boot, 0x25) |= dirty;
		f->boot->dirty = 1;
		return 0;
	}
	else if (fatbits(f) == 32) {
		_unit8uint(f->boot, 0x41) &= ~ (FAT_UNCLEAN | FAT_IOERROR);
		_unit8uint(f->boot, 0x41) |= dirty;
		f->boot->dirty = 1;
		return 0;
	}
	return -1;
}

/*
 * extended boot signature
 */

int fatgetextendedbootsignature(fat *f) {
	if (fatbits(f) == 12 || fatbits(f) == 16)
		return _unit8uint(f->boot, 0x26) == 0x29;
	else if (fatbits(f) == 32)
		return _unit8uint(f->boot, 0x42) == 0x29;
	else
		return FAT_ERR;
}
int fataddextendedbootsignature(fat *f) {
	if (fatbits(f) == 12 || fatbits(f) == 16)
		_unit8uint(f->boot, 0x26) = 0x29;
	else if (fatbits(f) == 32)
		_unit8uint(f->boot, 0x42) = 0x29;
	else
		return FAT_ERR;
	return 0;
}
int fatdeleteextendedbootsignature(fat *f) {
	if (fatbits(f) == 12 || fatbits(f) == 16)
		_unit8uint(f->boot, 0x26) = 0x0;
	else if (fatbits(f) == 32)
		_unit8uint(f->boot, 0x42) = 0x0;
	else
		return FAT_ERR;
	return 0;
}

uint32_t fatgetserialnumber(fat *f) {
	if (fatbits(f) == 12 || fatbits(f) == 16)
		return _unit32uint(f->boot, 0x27);
	else if (fatbits(f) == 32)
		return _unit32uint(f->boot, 0x43);
	else
		return FAT_ERR;
}
int fatsetserialnumber(fat *f, uint32_t serial) {
	if (fatbits(f) == 12 || fatbits(f) == 16) {
		_unit32uint(f->boot, 0x27) = serial;
		f->boot->dirty = 1;
	}
	else if (fatbits(f) == 32) {
		_unit32uint(f->boot, 0x43) = serial;
		f->boot->dirty = 1;
	}
	else
		return FAT_ERR;
	return 0;
}

char* fatgetvolumelabel(fat *f) {
	char *l = malloc(12);
	l[11] = '\0';
	if (fatbits(f) == 12 || fatbits(f) == 16)
		memcpy(l, fatunitgetdata(f->boot) + 0x2b, 11);
	else if (fatbits(f) == 32)
		memcpy(l, fatunitgetdata(f->boot) + 0x47, 11);
	else {
		free(l);
		return NULL;
	}
	return l;
}
int fatsetvolumelabel(fat *f, const char l[11]) {
	if (fatbits(f) == 12 || fatbits(f) == 16) {
		memcpy(fatunitgetdata(f->boot) + 0x2b, l, 11);
		f->boot->dirty = 1;
	}
	else if (fatbits(f) == 32) {
		memcpy(fatunitgetdata(f->boot) + 0x47, l, 11);
		f->boot->dirty = 1;
	}
	else
		return -1;
	return 0;
}

char* fatgetfilesystemtype(fat *f) {
	char *t = malloc(9);
	t[8] = '\0';
	if (fatbits(f) == 12 || fatbits(f) == 16)
		memcpy(t, fatunitgetdata(f->boot) + 0x36, 8);
	else if (fatbits(f) == 32)
		memcpy(t, fatunitgetdata(f->boot) + 0x52, 8);
	else {
		free(t);
		return NULL;
	}
	return t;
}
int fatsetfilesystemtype(fat *f, char t[8]) {
	if (fatbits(f) == 12 || fatbits(f) == 16) {
		memcpy(fatunitgetdata(f->boot) + 0x36, t, 8);
		f->boot->dirty = 1;
	}
	else if (fatbits(f) == 32) {
		memcpy(fatunitgetdata(f->boot) + 0x52, t, 8);
		f->boot->dirty = 1;
	}
	else
		return -1;
	return 0;
}

/*
 * backup sector
 */

int fatgetbackupsector(fat *f) {
	int b;
	b = le16toh(_unit16int(f->boot, 0x32));
	if (b == 0x0000 || b == 0xFFFF)
		return -1;
	return b;
}

int fatsetbackupsector(fat *f, int sector) {
	if (f == NULL || f->boot == NULL)
		return -1;

	if (sector == 0 || sector > fatgetreservedsectors(f) ||
	    (sector == fatgetinfopos(f)))
		return -1;

	_unit16int(f->boot, 0x32) = htole16(sector);
	f->boot->dirty = 1;
	return 0;
}

int fatcopyboottobackup(fat *f) {
	int n;
	unit *b;

	n = fatgetbackupsector(f);
	if (n == -1)
		return -1;

	b = fatunitcopy(f->boot);
	b->n = n;
	fatunitinsert(&f->sectors, b, 1);
	return 0;
}

/*
 * position of information sector, if any
 */

int fatgetinfopos(fat *f) {
	return le16toh(_unit16int(f->boot, 0x30));
}

int fatsetinfopos(fat *f, int pos) {
	_unit16int(f->boot, 0x30) = htole16(pos);
	f->boot->dirty = 1;
	return 0;
}

/*
 * media descriptor, mainly used for checking if a file is a fat filesystem
 */

uint8_t fatgetmedia(fat *f) {
	return _unit8int(f->boot, 0x15);
}

int fatsetmedia(fat *f, uint8_t media) {
	_unit8int(f->boot, 0x15) = media;
	f->boot->dirty = 1;
	return 0;
}

/*
 * boot sector signature
 */

int _fatbootsignaturepos(fat *f) {
	int b;
	b = fatgetbytespersector(f);
	return (b < 512 ? b : 512) - 2;
}

int fatgetbootsignature(fat *f) {
	uint16_t sig;
	int pos;

	if (f == NULL || f->boot == NULL) {
		printf("boot sector not loaded\n");
		exit(1);
	}

	pos = _fatbootsignaturepos(f);
	sig = le16toh(_unit16uint(f->boot, pos));

	dprintf("0x%04X\n", sig);
	return sig == 0xAA55;
}

int fatsetbootsignature(fat *f) {
	if (f == NULL || f->boot == NULL)
		return -1;
	_unit16uint(f->boot, _fatbootsignaturepos(f)) = htole16(0xAA55);
	f->boot->dirty = 1;
	return 0;
}

/*
 * information sector signatures
 */

int fatgetinfosignatures(fat *f) {
	uint32_t first, second;
	uint16_t third;

	if (f->info == NULL)
		return -1;

	first =  le32toh(_unit32uint(f->info, 0x000));
	second = le32toh(_unit32uint(f->info, 0x1E4));
	third =  le16toh(_unit16uint(f->info, 0x1FE));

	dprintf("0x%08X\n", first);
	dprintf("0x%08X\n", second);
	dprintf("0x%04X\n", third);

	return first == 0x41615252 && second == 0x61417272 && third == 0xAA55;
}

int fatsetinfosignatures(fat *f) {
	if (f->info == NULL)
		return -1;

	_unit32uint(f->info, 0x000) = htole32(0x41615252);
	_unit32uint(f->info, 0x1E4) = htole32(0x61417272);
	_unit16uint(f->info, 0x1FE) = htole16(0xAA55);

	f->info->dirty = 1;

	return 0;
}

/*
 * last allocated cluster
 */

int32_t fatgetlastallocatedcluster(fat *f) {
	if (f->info == NULL)
		return -1;
	return le32toh(_unit32int(f->info, 0x1EC));
}

void fatsetlastallocatedcluster(fat *f, int32_t last) {
	if (f->info == NULL)
		return;

	_unit32int(f->info, 0x1EC) = htole32(last);
	f->info->dirty = 1;
}

/*
 * number of free clusters
 */

int32_t fatgetfreeclusters(fat *f) {
	if (f->info == NULL)
		return -1;
	return le32toh(_unit32int(f->info, 0x1E8));
}

void fatsetfreeclusters(fat *f, int32_t last) {
	if (f->info == NULL)
		return;

	_unit32int(f->info, 0x1E8) = htole32(last);
	f->info->dirty = 1;
}

/*
 * summary of fs
 */
void fatsummary(fat *f) {
	uint64_t space;

	printf("filesystem: FAT%d\n", fatbits(f));
	printf("bytes per sector: %d\n", fatgetbytespersector(f));
	printf("sectors per cluster: %d\n", fatgetsectorspercluster(f));
	printf("bytes per cluster: %d\n", fatbytespercluster(f));
	printf("reserved sectors: %d\n", fatgetreservedsectors(f));
	printf("number of fats: %d\n", fatgetnumfats(f));
	printf("sectors: %d\n", fatgetnumsectors(f));
	printf("size of each fat: %d\n", fatgetfatsize(f));
	printf("first cluster of root directory: %d\n", fatgetrootbegin(f));
	printf("max entries in root directory: %d\n", fatgetrootentries(f));
	printf("number of data clusters: %d\n", fatnumdataclusters(f));
	printf("data clusters: %d - %d\n", 2, fatlastcluster(f));
	printf("signature: %s\n", fatgetbootsignature(f) ? "yes" : "no");
	if (fatgetextendedbootsignature(f)) {
		printf("extended boot signature:\n");
		printf("  serial number: 0x%08X\n", fatgetserialnumber(f));
		printf("  volume label: \"%s\"\n", fatgetvolumelabel(f));
		printf("  filesystem type: \"%s\"\n", fatgetfilesystemtype(f));
	}
	if (f->info != NULL) {
		printf("information sector (%d):\n", f->info->n);
		printf("  information sector signature: %s\n",
			fatgetinfosignatures(f) ? "yes" : "no");
		printf("  last known allocated cluster: %d\n",
			fatgetlastallocatedcluster(f));
		printf("  free clusters, maybe: %d\n", fatgetfreeclusters(f));
		space = fatgetfreeclusters(f) *
			(uint64_t) fatbytespercluster(f);
		printf("  free space, maybe: %" PRIu64, space);
		printf(" = %" PRIu64 "k", space /= 1024);
		printf(" = %" PRIu64 "M", space /= 1024);
		printf(" = %" PRIu64 "G\n", space /= 1024);
	}
}

