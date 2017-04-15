/*
 * entry.h
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
 * entry.h
 *
 * read and write data in a directory entry
 *
 * every file in the filesystem has a 32-bytes entry that contains its short
 * name, size and first cluster; each directory contains some such entries in
 * one or more clusters
 *
 * all functions in this modules take as a parameter the pair cluster/index
 * that identifies the entry for a file
 */

#ifdef _DIRECTORY_H
#else
#define _DIRECTORY_H

#include <time.h>
#include "unit.h"

#define FAT_ATTR_RO      0x01
#define FAT_ATTR_HIDDEN  0x02
#define FAT_ATTR_SYSTEM  0x04
#define FAT_ATTR_VOLUME  0x08
#define FAT_ATTR_DIR     0x10
#define FAT_ATTR_ARCHIVE 0x20

#define FAT_ATTR_ALL \
	(FAT_ATTR_RO | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | \
	FAT_ATTR_VOLUME | FAT_ATTR_DIR | FAT_ATTR_ARCHIVE)
#define FAT_ATTR_LONGNAME \
	(FAT_ATTR_RO | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME)

/*
 * is an entry actually a file? part of a long name? is the directory over?
 */
int fatentryexists(unit *directory, int index);
int fatentryend(unit *directory, int index);
int fatentryislongpart(unit *directory, int index);

/*
 * get and set parts of a directory entry
 */
void fatshortnametostring(char dst[13], unsigned char src[11]);
int fatstringtoshortname(unsigned char dst[11], char src[13]);
void fatentrygetshortname(unit *directory, int index, char shortname[13]);
int fatentrysetshortname(unit *directory, int index, char *shortname);
void fatentryprintshortname(unit *directory, int index);
int fatentrycompareshortname(unit *directory, int index, char shortname[13]);
int fatentryisdotfile(unit *directory, int index);

int32_t fatentrygetfirstcluster(unit *directory, int index, int bits);
int fatentrysetfirstcluster(unit *directory, int index, int bits, int32_t n);

unsigned char fatentrygetattributes(unit *directory, int index);
void fatentrysetattributes(unit *directory, int index, unsigned char attr);
int fatentryisdirectory(unit *directory, int index);

uint32_t fatentrygetsize(unit *directory, int index);
void fatentrysetsize(unit *directory, int index, uint32_t size);

int fatentrygetwritetime(unit *directory, int index, struct tm *tm);
int fatentrygetcreatetime(unit *directory, int index, struct tm *tm);
int fatentrygetreadtime(unit *directory, int index, struct tm *tm);

int fatentrysetwritetime(unit *directory, int index, struct tm *tm);
int fatentrysetcreatetime(unit *directory, int index, struct tm *tm);
int fatentrysetreadtime(unit *directory, int index, struct tm *tm);

int fatentrysetwritetimenow(unit *directory, int index);
int fatentrysetcreatetimenow(unit *directory, int index);
int fatentrysetreadtimenow(unit *directory, int index);

/*
 * delete a directory entry
 * zeroing it is only for initialization and when no other entry follows
 */
void fatentrydelete(unit *directory, int index);
void fatentryzero(unit *directory, int index);

/*
 * print a summary of a directory entry
 */
void fatentryprintpos(unit *directory, int index, int minsize);
void fatentryprint(unit *directory, int index);

#endif

