/*
 * fs.h
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
 * fs.h
 *
 * the global data of a fat filesystem: the structure representing it and the
 * functions for reading/flushing/closing and for accessing the global
 * parameters (number of sectors, etc)
 */

#ifdef _FS_H
#else
#define _FS_H

#include <stdint.h>
#include "unit.h"

/*
 * an open fat device or image
 */
typedef struct {
	int fd;
	char *devicename;
	uint64_t offset;

	int bits;				/* 12, 16 or 32 */
	int nfat;				/* file alloc. table to use */
	int insensitive;			/* case-insensitive lookup? */

	unit *boot;				/* boot sector */
	unit *info; 				/* fs info sector (fat32) */
	unit *sectors;				/* cache for sectors */
	unit *clusters;				/* cache for clusters */

	int32_t last;				/* last found free cluster */
	int32_t free;				/* number of free clusters */

	void *user;				/* free for program use */
} fat;

/*
 * global operations on a fat: create, read, check, flush, quit and close
 */
fat *fatcreate();
fat *fatopen(char *filename, off_t offset);
fat *fatsignatureopen(char *filename, off_t offset);
int fatcheck(fat *f);
int fatflush(fat *f);
int fatquit(fat *f);
int fatclose(fat *f);

/*
 * global parameters of a fat
 */
int fatbits(fat *f);
int fatsignaturebits(fat *f);
int fatgetbytespersector(fat *f);
int fatsetbytespersector(fat *f, int bytes);
uint8_t fatgetsectorspercluster(fat *f);
int fatsetsectorspercluster(fat *f, int sectors);
int fatbytespercluster(fat *f);
int fatgetreservedsectors(fat *f);
int fatsetreservedsectors(fat *f, int sectors);
int fatgetnumfats(fat *f);
int fatsetnumfats(fat *f, int nfat);
uint32_t fatgetnumsectors(fat *f);
int fatsetnumsectors(fat *f, uint32_t sectors);
int fatgetfatsize16(fat *f);
int fatsetfatsize16(fat *f, int size);
int fatgetfatsize32(fat *f);
int fatsetfatsize32(fat *f, int size);
int fatgetfatsize(fat *f);
int fatsetfatsize(fat *f, int size);
int32_t fatgetrootbegin(fat *f);
int fatsetrootbegin(fat *f, int32_t num);
int fatgetrootentries(fat *f);
int fatsetrootentries(fat *f, int entries);
int32_t fatnumrootsectors(fat *f);
int32_t fatnumdataclusters(fat *f);
int32_t fatlastcluster(fat *f);
int fatisvalidcluster(fat *f, int32_t n);
#define FAT_UNCLEAN 0x01
#define FAT_IOERROR 0x02
int fatgetdirtybits(fat *f);
int fatsetdirtybits(fat *f, int dirty);
int fatgetextendedbootsignature(fat *f);
int fataddextendedbootsignature(fat *f);
int fatdeleteextendedbootsignature(fat *f);
uint32_t fatgetserialnumber(fat *f);
int fatsetserialnumber(fat *f, uint32_t serial);
char* fatgetvolumelabel(fat *f);
int fatsetvolumelabel(fat *f, const char l[11]);
char* fatgetfilesystemtype(fat *f);
int fatsetfilesystemtype(fat *f, char t[8]);
int fatgetbackupsector(fat *f);
int fatcopyboottobackup(fat *f);
int fatgetinfopos(fat *f);
int fatsetinfopos(fat *f, int pos);
uint8_t fatgetmedia(fat *f);
int fatsetmedia(fat *f, uint8_t media);
int fatgetbootsignature(fat *f);
int fatsetbootsignature(fat *f);

/*
 * fs information sector
 */
int fatgetinfosignatures(fat *f);
int fatsetinfosignatures(fat *f);
int32_t fatgetlastallocatedcluster(fat *f);		/* README: Note 4 */
void fatsetlastallocatedcluster(fat *f, int32_t last);
int32_t fatgetfreeclusters(fat *f);
void fatsetfreeclusters(fat *f, int32_t last);

/*
 * summary of fs
 */
void fatsummary(fat *f);

#endif

