/*
 * long.h
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

#include <wchar.h>
#include "fs.h"

/*
 * wide strings
 */
#define WNULL ((wchar_t) (L'\0'))

/*
 * find next directory entry, possibly with long filename
 */
#define FAT_END       0x8000
#define FAT_SHORT     0x4000
#define FAT_LONG_ALL  0x2000
#define FAT_LONG_SOME 0x1000
#define FAT_LONG_ERR  0x0800

struct fatlongscan {
	int n;
	uint8_t checksum;
	unit *longdirectory;
	int longindex;
	wchar_t *name;
	int len;
	int err;
};

void fatlonginit(struct fatlongscan *scan);
void fatlongend(struct fatlongscan *scan);
int fatlongscan(unit *directory, int index, struct fatlongscan *scan);

int fatlongnext(fat *f, unit **directory, int *index,
		unit **longdirectory, int *longindex, wchar_t **name);
int fatnextname(fat *f, unit **directory, int *index, wchar_t **name);

/*
 * long file name lookup
 */
int fatlookupfilelongboth(fat *f, int32_t dir, wchar_t *name,
		unit **directory, int *index,
		unit **longdirectory, int *longindex);
int fatlookupfilelong(fat *f, int32_t dir, wchar_t *name,
		unit **directory, int *index);
int32_t fatlookupfirstclusterlong(fat *f, int32_t dir, wchar_t *name);

/*
 * path lookup
 */
int fatlookuppathlongboth(fat *f, int32_t dir, wchar_t *path,
		unit **directory, int *index,
		unit **longdirectory, int *longindex);
int fatlookuppathlong(fat *f, int32_t dir, wchar_t *path,
		unit **directory, int *index);
int32_t fatlookuppathfirstclusterlong(fat *f, int32_t dir, wchar_t *path);

/*
 * find free entries
 */
int fatfindfreelong(fat *f, int len, unit **directory, int *index,
		unit **startdirectory, int *startindex);
int fatfindfreelongpath(fat *f, int32_t dir, wchar_t *path, int len,
		unit **directory, int *index,
		unit **startdirectory, int *startindex);

/*
 * file names
 */
int fatinvalidnamelong(const wchar_t *name);
int fatinvalidpathlong(const wchar_t *path);
wchar_t *fatlegalizenamelong(const wchar_t *path);
wchar_t *fatlegalizepathlong(const wchar_t *path);
wchar_t *fatstoragenamelong(const wchar_t *name);
wchar_t *fatstoragepathlong(const wchar_t *path);

/*
 * create an empty file
 */
int fatcreatefileshortlong(fat *f, int32_t dir,
		unsigned char shortname[11], unsigned char casebyte,
		wchar_t *longname,
		unit **directory, int *index,
		unit **startdirectory, int *startindex);
int fatcreatefilelongboth(fat *f, int32_t dir, wchar_t *longname,
		unit **directory, int *index,
		unit **startdirectory, int *startindex);
int fatcreatefilelong(fat *f, int32_t dir, wchar_t *longname,
		unit **directory, int *index);
int fatcreatefilelongbothpath(fat *f, int32_t dir, wchar_t *path,
		unit **directory, int *index,
		unit **startdirectory, int *startindex);
int fatcreatefilelongpath(fat *f, int32_t dir, wchar_t *path,
		unit **directory, int *index);

/*
 * free a long file name (does not free its short name entry)
 */
int fatdeletelong(fat *f, unit *directory, int index);

/*
 * fatreferenceexecute(), longname version
 */
typedef int(* refrunlong)(fat *f,
	unit *directory, int index, int32_t previous,
	unit *startdirectory, int startindex, int32_t startprevious,
	unit *dirdirectory, int dirindex, int32_t dirprevious,
	wchar_t *name, int err, unit *longdirectory, int longindex,
	int direction, void *user);
int fatreferenceexecutelong(fat *f,
		unit *directory, int index, int32_t previous,
		refrunlong act, void *user);

void fatdumplong(fat *f, unit* directory, int index, int32_t previous,
		int recur, int all, int chains);

/*
 * fatfileexexcute(), longname version
 */
typedef void (* longrun)(fat *f, wchar_t *path, unit *directory, int index,
		wchar_t *name, int err, unit *longdirectory, int longindex,
		void *user);
int fatfileexecutelong(fat *f, unit *directory, int index, int32_t previous,
		longrun act, void *user);

/*
 * go back to the start of a long name
 */
int fatshortentrytolong(fat *f, fatinverse *rev,
		unit *directory, int index,
		unit **longdirectory, int *longindex);
int fatlongreferencetoentry(fat *f, fatinverse *rev,
		unit **directory, int *index, int32_t *previous,
		unit **longdirectory, int *longindex);
int fatshortentrytolongname(fat *f, fatinverse *rev,
		unit *directory, int index, wchar_t **longname);
wchar_t *fatinversepathlong(fat *f, fatinverse *rev,
		unit *directory, int index, int32_t previous);

