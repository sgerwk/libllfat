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
#include <stdint.h>
#include <inttypes.h>
#include "fs.h"
#include "boot.h"
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
 * initialize a fat structure; nothing is read from a device
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
 * open a filesystem and read the boot sector, but do not check for errors
 */
fat *fatopenonly(char *filename, off_t offset) {
	fat *f;

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

	return f;
}

/*
 * open a filesystem; also read the boot and possibly the information sector
 */
fat *_fatopen(char *filename, off_t offset, int (*fatbits)(fat *f)) {
	fat *f;
	int32_t info;

	f = fatopenonly(filename, offset);

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

	res = fatbootcheck(f->boot);

				// no short images
	dprintf("size: %ld\n", _sizeofdevice(f->fd));
	if (0 && _sizeofdevice(f->fd) > 0 &&
	    fatgetnumsectors(f) >
	    (unsigned long)_sizeofdevice(f->fd) / fatgetbytespersector(f))
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
 * determine bits of FAT: 12, 16 or 32
 */
int fatbits(fat *f) {
	if (f->bits != 0)
		return f->bits;
	f->bits = fatbootbits(f->boot);
	return f->bits;
}

/*
 * bits of FAT, according to the signature
 * not right according to the official spec
 */
int fatsignaturebits(fat *f) {
	return fatbootsignaturebits(f->boot);
}

/*
 * bytes per sector
 */
int fatgetbytespersector(fat *f) {
	return fatbootgetbytespersector(f->boot);
}

int fatsetbytespersector(fat *f, int bytes) {
	if (f == NULL)
		return -1;
	return fatbootsetbytespersector(f->boot, bytes);
}

/*
 * sectors per cluster
 */
uint8_t fatgetsectorspercluster(fat *f) {
	return fatbootgetsectorspercluster(f->boot);
}

int fatsetsectorspercluster(fat *f, int sectors) {
	if (f == NULL)
		return -1;
	return fatbootsetsectorspercluster(f->boot, sectors);
}

/*
 * bytes per clusters
 */
int fatbytespercluster(fat *f) {
	return fatbootbytespercluster(f->boot);
}

/*
 * reserved sectors
 */
int fatgetreservedsectors(fat *f) {
	return fatbootgetreservedsectors(f->boot);
}

int fatsetreservedsectors(fat *f, int sectors) {
	if (f == NULL)
		return -1;
	return fatbootsetreservedsectors(f->boot, sectors);
}

/*
 * number of fats
 */

int fatgetnumfats(fat *f) {
	return fatbootgetnumfats(f->boot);
}

int fatsetnumfats(fat *f, int nfat) {
	if (f == NULL)
		return -1;
	return fatbootsetnumfats(f->boot, nfat);
}

/*
 * total number of sectors in the filesystem
 */

uint32_t fatgetnumsectors(fat *f) {
	return fatbootgetnumsectors(f->boot, &f->bits);
}

int fatsetnumsectors(fat *f, uint32_t sectors) {
	return fatbootsetnumsectors(f->boot, &f->bits, sectors);
}

/*
 * size of one fat: 16-bit version, 32-bit version, fatbits-dependent version
 */

int fatgetfatsize16(fat *f) {
	if (f == NULL)
		return -1;
	return fatbootgetfatsize16(f->boot);
}

int fatsetfatsize16(fat *f, int size) {
	if (f == NULL)
		return -1;
	return fatbootsetfatsize16(f->boot, size);
}

int fatgetfatsize32(fat *f) {
	if (f == NULL)
		return -1;
	return fatbootgetfatsize32(f->boot);
}

int fatsetfatsize32(fat *f, int size) {
	if (f == NULL)
		return -1;
	return fatbootsetfatsize32(f->boot, size);
}

int fatgetfatsize(fat *f) {
	if (f == NULL)
		return -1;
	return fatbootgetfatsize(f->boot, &f->bits);
}

int fatsetfatsize(fat *f, int size) {
	if (f == NULL)
		return -1;
	return fatbootsetfatsize(f->boot, &f->bits, size);
}

/*
 * minimal size of a fat for a given number of clusters
 */
int fatminfatsize(fat *f, int32_t nclusters) {
	return fatbootminfatsize(f->boot, nclusters);
}

/*
 * set the size of a fat to its minimum possible value
 */
int fatbestfatsize(fat *f) {
	return fatbootbestfatsize(f->boot);
}

/*
 * check consistency of sizes in a filesystem
 */
int fatconsistentsize(fat *f) {
	return fatbootconsistentsize(f->boot, &f->bits);
}

/*
 * first cluster of root directory
 */

int32_t fatgetrootbegin(fat *f) {
	return fatbootgetrootbegin(f->boot, &f->bits);
}

int fatsetrootbegin(fat *f, int32_t num) {
	return fatbootsetrootbegin(f->boot, &f->bits, num);
}

/*
 * max number of entries in the root directory, zero for FAT32 (no limit)
 */

int fatgetrootentries(fat *f) {
	return fatbootgetrootentries(f->boot, &f->bits);
}

int fatsetrootentries(fat *f, int entries) {
	if (f == NULL)
		return -1;
	return fatbootsetrootentries(f->boot, &f->bits, entries);
}

/*
 * number of sectors for the root directory
 */
int32_t fatnumrootsectors(fat *f) {
	return fatbootnumrootsectors(f->boot, &f->bits);
}

/*
 * number of data clusters (first cluster is 2)
 */
int32_t fatnumdataclusters(fat *f) {
	return fatbootnumdataclusters(f->boot, &f->bits);
}

/*
 * last cluster
 */
int32_t fatlastcluster(fat *f) {
	return fatbootlastcluster(f->boot, &f->bits);
}

/*
 * check if a cluster number is valid
 */
int fatisvalidcluster(fat *f, int32_t n) {
	return fatbootisvalidcluster(f->boot, &f->bits, n);
}

/*
 * dirty bits
 */

int fatgetdirtybits(fat *f) {
	return fatbootgetdirtybits(f->boot, &f->bits);
}

int fatsetdirtybits(fat *f, int dirty) {
	return fatbootsetdirtybits(f->boot, &f->bits, dirty);
}

/*
 * extended boot signature
 */

int fatgetextendedbootsignature(fat *f) {
	return fatbootgetextendedbootsignature(f->boot, &f->bits);
}
int fataddextendedbootsignature(fat *f) {
	return fatbootaddextendedbootsignature(f->boot, &f->bits);
}
int fatdeleteextendedbootsignature(fat *f) {
	return fatbootdeleteextendedbootsignature(f->boot, &f->bits);
}

uint32_t fatgetserialnumber(fat *f) {
	return fatbootgetserialnumber(f->boot, &f->bits);
}
int fatsetserialnumber(fat *f, uint32_t serial) {
	return fatbootsetserialnumber(f->boot, &f->bits, serial);
}

char* fatgetvolumelabel(fat *f) {
	return fatbootgetvolumelabel(f->boot, &f->bits);
}
int fatsetvolumelabel(fat *f, const char l[11]) {
	return fatbootsetvolumelabel(f->boot, &f->bits, l);
}

char* fatgetfilesystemtype(fat *f) {
	return fatbootgetfilesystemtype(f->boot, &f->bits);
}
int fatsetfilesystemtype(fat *f, char t[8]) {
	return fatbootsetfilesystemtype(f->boot, &f->bits, t);
}

/*
 * backup sector
 */

int fatgetbackupsector(fat *f) {
	return fatbootgetbackupsector(f->boot);
}

int fatsetbackupsector(fat *f, int sector) {
	return fatbootsetbackupsector(f->boot, sector);
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
	return fatbootgetinfopos(f->boot);
}

int fatsetinfopos(fat *f, int pos) {
	return fatbootsetinfopos(f->boot, pos);
}

/*
 * media descriptor, mainly used for checking if a file is a fat filesystem
 */

uint8_t fatgetmedia(fat *f) {
	return fatbootgetmedia(f->boot);
}

int fatsetmedia(fat *f, uint8_t media) {
	return fatbootsetmedia(f->boot, media);
}

/*
 * boot sector signature
 */

int fatgetbootsignature(fat *f) {
	if (f == NULL) {
		printf("boot sector not loaded\n");
		exit(1);
	}
	return fatbootgetbootsignature(f->boot);
}

int fatsetbootsignature(fat *f) {
	if (f == NULL)
		return -1;
	return fatbootsetbootsignature(f->boot);
}

/*
 * information sector signatures
 */

int fatgetinfosignatures(fat *f) {
	return fatinfogetinfosignatures(f->info);
}

int fatsetinfosignatures(fat *f) {
	return fatinfosetinfosignatures(f->info);
}

/*
 * last allocated cluster
 */

int32_t fatgetlastallocatedcluster(fat *f) {
	return fatinfogetlastallocatedcluster(f->info);
}

void fatsetlastallocatedcluster(fat *f, int32_t last) {
	return fatinfosetlastallocatedcluster(f->info, last);
}

/*
 * number of free clusters
 */

int32_t fatgetfreeclusters(fat *f) {
	return fatinfogetfreeclusters(f->info);
}

void fatsetfreeclusters(fat *f, int32_t last) {
	fatinfosetfreeclusters(f->info, last);
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
	printf("sectors: %u\n", fatgetnumsectors(f));
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

