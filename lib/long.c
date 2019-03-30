/*
 * long.c
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
 * long.c
 *
 * long file names
 *
 * short filenames are ASCII; long filenames are UCS-2, but are internally
 * stored as wide character strings (wchar_t *); these can be converted to and
 * from both UCS-2 and ASCII
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h>
#include <iconv.h>
#include <errno.h>
#include "entry.h"
#include "directory.h"
#include "table.h"
#include "reference.h"
#include "inverse.h"
#include "long.h"

int fatlongdebug = 0;
#define dprintf if (fatlongdebug) printf

#define ENTRYPOS(directory, index, pos)			\
	(fatunitgetdata((directory))[(index) * 32 + (pos)])

#define MIN(x,y) (((x)<(y))?(x):(y))

/*
 * ucs2 type, length function and print
 */

typedef uint16_t ucs2char;

size_t ucs2len(ucs2char *s) {
	int i;
	for (i = 0; s[i] != 0; i++)
		;
	return i;
}

/*
 * multibyte character conversion
 *
 * only works for fixed-length characters like ASCII, UCS-2 and wchar_t
 *
 * if dst!=NULL, the result is stored in dst; otherwise, is stored in a string
 * allocated by this function; deallocate with free(3) when done with it
 *
 * if len=-1 the length of the source string is calculated from it
 *
 * if err!=NULL and a conversion error occurs, *err is increased (note:
 * INCREASED, not set); in particular: if some caracters could not be converted
 * their number is added to *err; other errors lead to an increase of
 * 1000*errno, but these should not occur for the particular conversions used
 * in this library
 */

#define MULTIBYTECONVERT(funcname, dsttype, dstname, 			\
	srctype, srcname, srclenfunc)					\
dsttype *funcname(dsttype *dst, srctype *src, int len, int *err) {	\
	dsttype *dstscan;						\
	size_t srclen, dstlen;						\
	iconv_t cd;							\
	int res;							\
									\
	if (len == -1)							\
		len = srclenfunc(src) + 1;				\
	srclen = len * sizeof(srctype);					\
	dstlen = len * sizeof(dsttype);					\
									\
	if (dst == NULL)						\
		dst = malloc(dstlen);					\
	dstscan = dst;							\
									\
	cd = iconv_open(dstname "//TRANSLIT", srcname );		\
	res = iconv(cd, (char **) &src, (size_t *) &srclen,		\
			(char **) &dstscan, &dstlen);			\
	if (res > 0) {							\
		printf("%d characters not converted\n", res);		\
		if (err)						\
			*err += res;					\
	}								\
	else if (res == -1) {						\
		perror("iconv");					\
		if (err)						\
			*err += 1000 * errno;				\
	}								\
	iconv_close(cd);						\
									\
	if (srclen != 0)						\
		printf("%zu characters not converted\n", srclen);	\
	if (dstlen != 0)						\
		printf("%zu characters not filled\n", dstlen);		\
									\
	return dst;							\
}

MULTIBYTECONVERT(fatucs2tows, wchar_t, "WCHAR_T", ucs2char, "UCS-2LE", ucs2len)
MULTIBYTECONVERT(fatwstoucs2, ucs2char, "UCS-2LE", wchar_t, "WCHAR_T", wcslen)

MULTIBYTECONVERT(fatchartows, wchar_t, "WCHAR_T", char, "ASCII", strlen)
MULTIBYTECONVERT(fatwstochar, char, "ASCII", wchar_t, "WCHAR_T", wcslen)

/*
 * checksum of a directory entry
 */
uint8_t fatchecksum(unsigned char shortname[11]) {
	int pos;
	uint8_t res;

	res = 0;
	for (pos = 0; pos < 11; pos++) {
		res = ((res & 1) << 7) | (res >> 1);	// rotate right 1
		res += shortname[pos];			// sum
	}
	return res;
}

uint8_t fatentrychecksum(unit *directory, int index) {
	return fatchecksum(& ENTRYPOS(directory, index, 0));
}

/*
 * convert a shortname into a widestring
 */
wchar_t *_fatshorttowide(unit *directory, int index) {
	unsigned char entryname[11];
	char shortname[13];
	int i;

	memcpy(entryname, & ENTRYPOS(directory, index, 0), 11);

	if (ENTRYPOS(directory, index, 12) & 0x08)
		for (i=0; i<8; i++)
			entryname[i] = tolower(entryname[i]);
	if (ENTRYPOS(directory, index, 12) & 0x10)
		for (i=8; i<11; i++)
			entryname[i] = tolower(entryname[i]);

	fatshortnametostring(shortname, entryname);

	return fatchartows(NULL, shortname, -1, NULL);
}

/*
 * one-step scan for a long-short sequence of directory entries
 *
 * typical cycle is:
 *	for (index = 0, fatlonginit(&scan);
 *	     (res = fatlongscan(directory, index, &scan)) != FAT_END;
 *	     fatnextentry(f, &directory, &index)) {
 *		...
 *		s = wcsdup(scan.name); // if name needed outside of the loop
 *		...
 *	}
 *	fatscanend(&scan);
 *
 * if res is FAT_END the directory is finished
 *
 * if res contains FAT_SHORT then the entry is complete:
 * - the file name is in scan.longname; it derives from the longname entries
 *   if any, otherwise from the shortname; it is always a widestring
 * - the rest of the file data can be found via fatentry...(directory,index)
 * - the long name (if any) begins in longdirectory,longindex
 *
 * if res also contains FAT_LONG_ALL then the name derives from some longname
 * directory entries, not from the short entry
 *
 * res is FAT_LONG_SOME if the entries read so far are a correct beginning of a
 * long name; it is FAT_LONG_SOME | FAT_LONG_FIRST if it is the very first
 * entry of a long name
 */

void fatlonginit(struct fatlongscan *scan) {
	scan->n = -1;
	scan->name = NULL;
	scan->len = -1;
	scan->err = 0;
}

void fatlongend(struct fatlongscan *scan) {
	scan->n = -1;
	free(scan->name);
	scan->name = NULL;
	scan->len = -1;
	scan->err = 0;
}

void _fatscanstart(unit *directory, int index, struct fatlongscan *scan) {
	free(scan->name);
	scan->name = malloc(sizeof(wchar_t));
	scan->name[0] = WNULL;
	scan->len = 1;
	scan->err = 0;
	scan->longdirectory = directory;
	scan->longindex = index;
}

int fatlongscan(unit *directory, int index, struct fatlongscan *scan) {
	int first;

	if (fatentryend(directory, index)) {
		fatlongend(scan);
		return FAT_END;
	}

	if (! fatentryexists(directory, index)) {
		fatlongend(scan);
		return 0;
	}

	scan->n--;

	if (! fatentryislongpart(directory, index)) {
		if (scan->n-- == 0 &&
		    fatentrychecksum(directory, index) == scan->checksum)
			return FAT_SHORT | FAT_LONG_ALL;

		free(scan->name);
		scan->name = _fatshorttowide(directory, index);
		scan->longdirectory = directory;
		scan->longindex = index;
		return FAT_SHORT;
	}

	if (_unit8int(directory, index * 32) & 0x40) {
		scan->n = _unit8int(directory, index * 32) & 0x3F;
		scan->checksum = _unit8uint(directory, index * 32 + 13);
		_fatscanstart(directory, index, scan);
		first = FAT_LONG_FIRST;
	}
	else if (scan->checksum != _unit8uint(directory, index * 32 + 13) ||
	    scan->n <= 0 || 
	    scan->n != (_unit8int(directory, index * 32) & 0x3F)) {
		scan->n = -1;
		return 0;
	}
	else
		first = 0;

	scan->name = realloc(scan->name,
			(scan->len + 5 + 6 + 2) * sizeof(wchar_t));
	memmove(scan->name + 5 + 6 + 2, scan->name,
			scan->len * sizeof(wchar_t));
	fatucs2tows(scan->name,
			(ucs2char *) & ENTRYPOS(directory, index,  1), 5,
			&scan->err);
	fatucs2tows(scan->name + 5,
			(ucs2char *) & ENTRYPOS(directory, index, 14), 6,
			&scan->err);
	fatucs2tows(scan->name + 5 + 6,
			(ucs2char *) & ENTRYPOS(directory, index, 28), 2,
			&scan->err);
	scan->len += 5 + 6 + 2;

	return FAT_LONG_SOME | first;
}

/*
 * find the next valid directory entry
 *
 * - updated directory,index points to the short name entry
 * - longdirectory,longindex is the start of the file in the directory: the
 *   start of the long name if any, otherwise is the same as directory,index
 * - name is the name of the file, short or long
 *
 * return:
 * - FAT_SHORT			only a short directory entry
 * - FAT_SHORT | FAT_LONG_ALL	short and long name
 * - ... | FAT_LONG_ERR		errors in the long name
 * - FAT_END			no file found because directory finished
 *
 * if there is no need to distinguish between the first two cases, call
 * fatnextname() instead
 *
 * name is never NULL; free when done with it
 */
int fatlongnext(fat *f, unit **directory, int *index,
		unit **longdirectory, int *longindex, wchar_t **name) {
	int res;
	struct fatlongscan scan;

	for (fatlonginit(&scan);
	     (res = fatlongscan(*directory, *index, &scan)) != FAT_END &&
	     	! (res & FAT_SHORT);
	     fatnextentry(f, directory, index))
		;

	*longdirectory = scan.longdirectory;
	*longindex = scan.longindex;
	*name = scan.name;
	return res | (scan.err == 0 ? 0 : FAT_LONG_ERR);
}

/*
 * next valid directory entry
 *
 * - updated directory,index points to the short name directory entry
 * - name is the name of the file: long if any, otherwise the short name,
 *   properly capitalized
 *
 * this function is intended for lookup and scans that do not change the long
 * name; otherwise, the function to use is fatlongnext()
 */
int fatnextname(fat *f, unit **directory, int *index, wchar_t **name) {
	unit *longdirectory;
	int longindex;
	int res;

	res = fatlongnext(f, directory, index,
		&longdirectory, &longindex, name);
	return res == FAT_END ? -1 : 0;
}

/*
 * string matching, case sensitive or not depending on f->insensitive
 */
int _fatwcscmp(fat *f, const wchar_t *a, const wchar_t *b) {
	return f->insensitive ? wcscasecmp(a, b) : wcscmp(a, b);
}

/*
 * find a file with the given name
 *
 * input values of directory, index, longdirectory, longindex are ignored;
 * output are the position of the shortname directory entry and of the begin of
 * the longname
 *
 * return 0 if found, -1 otherwise
 */
int fatlookupfilelongboth(fat *f, int32_t dir, wchar_t *name,
		unit **directory, int *index,
		unit **longdirectory, int *longindex) {
	wchar_t *sname;
	uint32_t cl;
	int res;

	dprintf("lookup file %ls:", name);

	if (swscanf(name, L"entry:%d,%d", &cl, index) == 2) {
		if (cl == 0)
			cl = fatgetrootbegin(f);
		*directory = fatclusterread(f, cl);
		res = fatlongnext(f, directory, index,
			longdirectory, longindex, &sname);
		return ! (res & FAT_SHORT);
	}

	*directory = fatclusterread(f, dir);

	for (*index = 0;
	     fatlongnext(f, directory, index,
	     		longdirectory, longindex, &sname) != FAT_END;
	     fatnextentry(f, directory, index)) {
		dprintf(" %ls", sname);
		if (! _fatwcscmp(f, name, sname)) {
			dprintf(" <- (found)\n");
			free(sname);
			return 0;
		}
		free(sname);
	}

	dprintf(" (not found)\n");
	*directory = NULL;
	return -1;
}
 
int fatlookupfilelong(fat *f, int32_t dir, wchar_t *name,
		unit **directory, int *index) {
	unit *longdirectory;
	int longindex;

	return fatlookupfilelongboth(f, dir, name, directory, index,
		&longdirectory, &longindex);
}
 
/*
 * first cluster of file, given its long name
 */
int32_t fatlookupfirstclusterlong(fat *f, int32_t dir, wchar_t *name) {
	unit *directory;
	int index;
	int32_t cl;

	if (swscanf(name, L"cluster:%d", &cl) == 1)
		return cl;

	if (fatlookupfilelong(f, dir, name, &directory, &index))
		return FAT_ERR;

	return fatentrygetfirstcluster(directory, index, fatbits(f));
}

/*
 * look up a file given its path (long name)
 */
int fatlookuppathlongboth(fat *f, int32_t dir, wchar_t *path,
		unit **directory, int *index,
		unit **longdirectory, int *longindex) {
	wchar_t *end, *copy;
	int res;

	dprintf("%ls\n", path);

	end = wcschr(path, L'/');

	if (end == NULL)
		return fatlookupfilelongboth(f, dir, path,
			directory, index, longdirectory, longindex);

	if (end == path)
		return fatlookuppathlongboth(f, dir, path + 1,
			directory, index, longdirectory, longindex);

	if (end[1] == WNULL) {
		copy = wcsdup(path);
		copy[end - path] = WNULL;
		res = fatlookuppathlongboth(f, dir, copy,
			directory, index, longdirectory, longindex);
		free(copy);
		return res;
	}

	copy = wcsdup(path);
	copy[end - path] = '\0';
	dir = fatlookupfirstclusterlong(f, dir, copy);
	if (dir == 0)
		dir = fatgetrootbegin(f);
	if (dir == FAT_ERR) {
		dprintf("part of path not found: '%ls'\n", copy);
		free(copy);
		*directory = NULL;
		return -1;
	}

	dprintf("name '%ls', directory: %d\n", copy, dir);

	res = fatlookuppathlongboth(f, dir, copy + (end - path + 1),
		directory, index, longdirectory, longindex);

	if (! res) {
		dprintf("name '%ls':", copy + (end - path + 1));
		dprintf(" %d,%d\n", (*directory)->n, *index);
	}

	free(copy);
	return res;
}

int fatlookuppathlong(fat *f, int32_t dir, wchar_t *path,
		unit **directory, int *index) {
	unit *longdirectory;
	int longindex;

	return fatlookuppathlongboth(f, dir, path,
		directory, index, &longdirectory, &longindex);
}

/*
 * first cluster of file, given its long path
 */
int32_t fatlookuppathfirstclusterlong(fat *f, int32_t dir, wchar_t *path) {
	unit *directory;
	int index;

	if (! wcscmp(path, L"/"))
		return fatgetrootbegin(f);

	if (! wcschr(path, L'/'))
		return fatlookupfirstclusterlong(f, dir, path);

	if (fatlookuppathlong(f, dir, path, &directory, &index))
		return FAT_ERR;

	return fatentrygetfirstcluster(directory, index, fatbits(f));
}

/*
 * find the first sequence of len free directory entries
 */
int fatfindfreelong(fat *f, int len, unit **directory, int *index,
		unit **startdirectory, int *startindex) {
	int consecutive;
	unit *nextdirectory;
	int nextindex;

	if (fatfindfreeentry(f, directory, index))
		return -1;
	*startdirectory = *directory;
	*startindex = *index;
	consecutive = 1;

	while (consecutive < len) {
		nextdirectory = *directory;
		nextindex = *index;
		if (fatfindfreeentry(f, directory, index))
			return -1;
		fatnextentry(f, &nextdirectory, &nextindex);
		if (nextdirectory == *directory && nextindex == *index)
			consecutive++;
		else {
			consecutive = 1;
			*startdirectory = *directory;
			*startindex = *index;
		}
	}

	return 0;
}

/*
 * find the first sequence of len free entries in a directory given by path
 */
int fatfindfreelongpath(fat *f, int32_t dir, wchar_t *path, int len,
		unit **directory, int *index,
		unit **startdirectory, int *startindex) {
	int cl, r;

	r = fatgetrootbegin(f);

	if (path == NULL || ! wcscmp(path, L"") || ! wcscmp(path, L"/"))
		cl = r;
	else {
		if (path[0] == '/')
			dir = r;
		cl = fatlookuppathfirstclusterlong(f, dir, path);
		if (cl == FAT_ERR)
			return FAT_ERR;
	}

	dprintf("directory cluster: %d\n", cl);

	*directory = fatclusterread(f, cl);
	if (*directory == NULL)
		return -1;
	*index = -1;
	return fatfindfreelong(f, len, directory, index,
		startdirectory, startindex);
}

/*
 * check whether a file name or path is valid
 */
int fatinvalidnamelong(const wchar_t *name) {
	if (fatinvalidpathlong(name))
		return -1;

	if (wcschr(name, L'/'))
		return -1;

	if (! wcscmp(name, L".") || ! wcscmp(name, L".."))
		return 1;

	return 0;
}

int fatinvalidpathlong(const wchar_t *path) {
	wchar_t *scan, *last;

	if (wcspbrk(path, L"\"*:<>?\\|"))
		return -1;

	for (scan = (wchar_t *) path; *scan; scan++)
		if (*scan < 32)
			return -1;

	last = wcsrchr(path, '/');
	last = last == NULL ? (wchar_t *) path : last + 1;
	if (! wcscmp(last, L".") || ! wcscmp(last, L".."))
		return 1;

	return 0;
}

/*
 * legalize a path by escaping forbidden characters as [HH]
 */
wchar_t *_fatlegalize(const wchar_t *path, const wchar_t *illegal) {
	wchar_t *dst, *dscan, *scan;

	dst = malloc(wcslen(path) * sizeof(wchar_t) * 4);
	dscan = dst;

	for (scan = (wchar_t *) path; *scan; scan++)
		if (wcschr(illegal, *scan) || *scan < 32) {
			*dscan++ = L'[';
			swprintf(dscan, 4, L"%X", *scan);
			dscan += wcslen(dscan);
			*dscan++ = L']';
		}
		else {
			*dscan++ = *scan;
		}
	*dscan = '\0';

	return dst;
}
wchar_t *fatlegalizenamelong(const wchar_t *path) {
	return _fatlegalize(path, L"\"*:<>?\\|[]/");
}
wchar_t *fatlegalizepathlong(const wchar_t *path) {
	return _fatlegalize(path, L"\"*:<>?\\|[]");
}

/*
 * convert part of a string into the form that is used for a longname
 */
wchar_t *_fatstoragepartlong(wchar_t **dst, const wchar_t **src) {
	wchar_t *start, *end, *scan;

	dprintf("src: |%ls|\n", *src);

	while (**src != L'\0' && **src == ' ')
		(*src)++;

	end = wcschr(*src, L'/');
	end = end == NULL ? (wchar_t *) *src + wcslen(*src) : end;

	scan = end - 1;
	while (scan >= *src && (*scan == L' ' || *scan == L'.'))
		scan--;
	scan++;

	if (end - *src == 1 && scan[0] == L'.')
		scan = (wchar_t *) *src + 1;
	if (end - *src == 2 && scan[0] == L'.' && scan[1] == L'.')
		scan = (wchar_t *) *src + 2;

	wmemcpy(*dst, *src, scan - *src);

	start = *dst;
	*dst += scan - *src;
	**dst = *end;
	*src = end;

	dprintf("start: |%ls|\tdst: |%ls|\tleft: |%ls|\n", start, *dst, *src);
	return start;
}

/*
 * turn a name into the representation actually stored in the filesystem
 */
wchar_t *fatstoragenamelong(const wchar_t *name) {
	wchar_t *dst, *start;
	dst = malloc(sizeof(wchar_t) * (wcslen(name) + 1));
	start = _fatstoragepartlong(&dst, &name);
	if (*dst == '/')
		printf("WARNING: path passed as file to %s()\n", __func__);
	return start;
}

/*
 * convert a string into the form that is used for a path
 */
wchar_t *fatstoragepathlong(const wchar_t *path) {
	wchar_t *start, *dst;
	start = malloc(sizeof(wchar_t) * (wcslen(path) + 1));
	for (dst = start; _fatstoragepartlong(&dst, &path) && *path; ) {
		path++;
		dst++;
	}
	return start;
}

/*
 * store some part of a longname in directory,index
 */
void _fatsetlongpart(unit *directory, int index, wchar_t *part,
		int progressive, int first, int checksum) {

	fatentryzero(directory, index);

	fatwstoucs2((ucs2char *)
			& ENTRYPOS(directory, index, 1), part, 5, NULL);
	fatwstoucs2((ucs2char *)
			& ENTRYPOS(directory, index, 14), part + 5, 6, NULL);
	fatwstoucs2((ucs2char *)
			& ENTRYPOS(directory, index, 28), part + 5 + 6, 2,
			NULL);

	ENTRYPOS(directory, index, 0) = progressive | (first ? 0x40 : 0x00);
	ENTRYPOS(directory, index, 13) = checksum;
	fatentrysetattributes(directory, index, FAT_ATTR_LONGNAME);
}

/*
 * create an empty file from its short and long name, in a given directory
 */
int fatcreatefileshortlong(fat *f, int32_t dir,
		unsigned char shortname[11], unsigned char casebyte,
		wchar_t *longname,
		unit **directory, int *index,
		unit **startdirectory, int *startindex) {
	unit *scandirectory;
	int scanindex;
	int len, n, pos, i;
	wchar_t frag[14], wfiller;
	ucs2char filler;
	uint8_t checksum;

	dprintf("fatcreatefileshortlong: %11.11s %ls\n", shortname, longname);

	filler = 0xFFFF;
	fatucs2tows(&wfiller, &filler, 1, NULL);

	len = wcslen(longname);
	n = (len + 12) / 13 + 1;

	*directory = fatclusterread(f, dir);
	if (*directory == NULL) {
		dprintf("cannot read cluster %d\n", dir);
		return -1;
	}
	*index = -1;

	if (fatfindfreelong(f, n, directory, index,
			startdirectory, startindex)) {
		dprintf("not enough free entries for file\n");
		return -1;
	}

	checksum = fatchecksum(shortname);

	scandirectory = *startdirectory;
	scanindex = *startindex;
	for (pos = n - 1; pos > 0; pos--) {
		wmemcpy(frag, longname + (pos - 1) * 13,
			MIN(13, len - (pos - 1) * 13));
		frag[MIN(13, len - (pos - 1) * 13)] = WNULL;
		for (i = MIN(13, len - (pos - 1) * 13) + 1; i < 14; i++)
			frag[i] = wfiller;

		dprintf("%d,%d ", scandirectory->n, scanindex);
		dprintf("%d %ls\n", pos, frag);

		_fatsetlongpart(scandirectory, scanindex,
			frag, pos, pos == n - 1, checksum);

		fatnextentry(f, &scandirectory, &scanindex);
	}

	dprintf("%d,%d %.11s\n", scandirectory->n, scanindex, shortname);
	fatentryzero(*directory, *index);
	memcpy(& ENTRYPOS(*directory, *index, 0), shortname, 11);
 	ENTRYPOS(*directory, *index, 12) = casebyte;
	fatentrysetsize(*directory, *index, 0);
	fatentrysetfirstcluster(*directory, *index, f->bits, FAT_UNUSED);

	return 0;
}

/*
 * determine the short name of a file from its long name
 */

int _fatstringcase(wchar_t *s, int len) {
	int c;
	int i;

	c = 1000;
	for (i = 0; i < len && s[i]; i++)
		if (iswupper(s[i]) || s[i] == L'-')
			c = c != -1 && c != 0 ? 1 : 0;
		else if (iswlower(s[i]) || s[i] == L'-')
			c = c != 1 && c != 0 ? -1 : 0;
	return c;
}

void _fatwcstoupper(unsigned char *dst, wchar_t *src, int len) {
	int i;

	fatwstochar((char *) dst, src, len, NULL);
	for(i = 0; i < len; i++) 
		dst[i] = toupper(dst[i]);
}

int _fatshorttoshort(wchar_t *name,
		unsigned char shortname[11], unsigned char *casebyte) {
	wchar_t *dot, *ext;
	int casename, caseext;

	*casebyte = 0;

	if (! wcscmp(name, L".") || ! wcscmp(name, L"..")) {
		memcpy(shortname, ".          ", 11);
		shortname[1] = name[1] == L'.' ? '.' : ' ';
		return 0;
	}

	dot = wcschr(name, L'.');
	if (dot != NULL && wcschr(dot + 1, L'.'))
		return -1;

	if (dot == name)
		return -1;
	if (dot == NULL && wcslen(name) > 8)
		return -1;
	if (dot != NULL && dot - name > 8)
		return -1;
	if (dot != NULL && wcslen(dot + 1) > 3)
		return -1;

	ext = dot != NULL ? dot : name + wcslen(name);

	casename = _fatstringcase(name, MIN(8, ext - name));
	caseext = dot == NULL ? 1 :
		_fatstringcase(ext + 1,
			MIN(3, wcslen(name) - (ext + 1 - name)));
	dprintf("case byte: %d %d\n", casename, caseext);
	if (casename == 0 || caseext == 0)
		return -1;

	memset(shortname, ' ', 11);
	_fatwcstoupper(shortname, name, MIN(8, ext - name));
	if (dot != NULL)
		_fatwcstoupper(shortname + 8, ext + 1,
			MIN(3, wcslen(name) - (ext + 1 - name)));

	*casebyte |= caseext == -1 ? 0x10 : 00;
	*casebyte |= casename == -1 ? 0x08 : 00;

	return 0;
}

int _fatshortexists(fat *f, int32_t dir, unsigned char shortname[11]) {
	unit *directory;
	int index;

	directory = fatclusterread(f, dir);
	if (directory == NULL)
		return 0;

	for (index = -1;
	     ! fatnextentry(f, &directory, &index); )
		if (! memcmp(& ENTRYPOS(directory, index, 0), shortname, 11))
			return 1;

	return 0;
}

int _fatlongtoshort(fat *f, int32_t dir, wchar_t *name,
		unsigned char shortname[11]) {
	unsigned char stem[11], num[9];
	wchar_t *dot;
	int i, n;

	memset(stem, ' ', 11);
	fatwstochar((char *) stem, name, 8, NULL);

	dot = wcsrchr(name, L'.');
	if (dot != NULL) {
		if (dot - name < 8)
			memset(stem + (dot - name), ' ', 8 - (dot - name));
		fatwstochar((char *) stem + 8, dot + 1,
			MIN(3, wcslen(dot + 1)), NULL);
	}

	for (i = 0; i < 11; i++)
		if (! isalnum(stem[i]))
			stem[i] = '_';
		else if (! isupper(stem[i]))
			stem[i] = toupper(stem[i]);

	// dprintf("%.11s\n", stem);
	memcpy(shortname, stem, 11);
	if (_fatshortexists(f, dir, shortname) == 0)
		return 0;

	for (n = 1; n < 99999; n++) {
		sprintf((char *) num, "%+8d", n);

		for (i = 0; i < 11; i++)
			if (i >= 8 || num[i] == ' ')
				shortname[i] = stem[i];
			else if (num[i] == '+')
				shortname[i] = '~';
			else
				shortname[i] = num[i];

		// dprintf("%.11s\n", shortname);
		if (_fatshortexists(f, dir, shortname) == 0)
			return 0;
	}

	return -1;
}

/*
 * create an empty file from its long name only, in a given directory
 */
int fatcreatefilelongboth(fat *f, int32_t dir, wchar_t *name,
		unit **directory, int *index,
		unit **startdirectory, int *startindex) {
	unsigned char shortname[11];
	unsigned char casebyte;

	if (name[0] == L'\0')
		return -1;

	casebyte = 0;

	if (! _fatshorttoshort(name, shortname, &casebyte))
		name = L"";
	else if (_fatlongtoshort(f, dir, name, shortname))
		return -1;

	dprintf("shortname: |%11.11s|\t\tlongname: |%ls|\n", shortname, name);

	return fatcreatefileshortlong(f, dir, shortname, casebyte, name,
		directory, index, startdirectory, startindex);
}

int fatcreatefilelong(fat *f, int32_t dir, wchar_t *name,
		unit **directory, int *index) {
	unit *startdirectory;
	int startindex;
	return fatcreatefilelongboth(f, dir, name, directory, index,
		&startdirectory, &startindex);
}

/*
 * create an empty file from its complete long path
 */
int fatcreatefilelongbothpath(fat *f, int32_t dir, wchar_t *path,
		unit **directory, int *index,
		unit **startdirectory, int *startindex) {
	wchar_t *buf, *slash, *dirname, *file;
	int res;

	buf = wcsdup(path);
	slash = wcsrchr(buf, L'/');
	if (slash == NULL) {
		dirname = L"/";
		file = path;
	}
	else if (slash == buf) {
		dirname = L"/";
		file = buf + 1;
	}
	else {
		dirname = buf;
		*slash = WNULL;
		file = slash + 1;
	}

	dprintf("path %ls, file %ls\n", dirname, file);

	dir = fatlookuppathfirstclusterlong(f, dir, dirname);
	if (dir == FAT_ERR) {
		free(buf);
		return -1;
	}
	if (dir == 0)
		dir = fatgetrootbegin(f);

	res = fatcreatefilelongboth(f, dir, file,
			directory, index, startdirectory, startindex);
	free(buf);
	return res;
}

int fatcreatefilelongpath(fat *f, int32_t dir, wchar_t *path,
		unit **directory, int *index) {
	unit *startdirectory;
	int startindex;
	return fatcreatefilelongbothpath(f, dir, path, directory, index,
		&startdirectory, &startindex);
}

/*
 * free a long file name (does not free its short name entry)
 */
int fatdeletelong(fat *f, unit *directory, int index) {
	struct fatlongscan scan;
	int res, lastn;

	lastn = -1;
	for (fatlonginit(&scan);
	     (res = fatlongscan(directory, index, &scan)) == FAT_LONG_SOME;
	     fatnextentry(f, &directory, &index)) {
		dprintf("delete entry %d,%d ", directory->n, index);
		dprintf("(num %d)\n", scan.n);
		fatentrydelete(directory, index);
		lastn = scan.n;
	}
	fatlongend(&scan);

	return res & FAT_LONG_ALL ? 0 : lastn == 1 ? -1 : -2;
}

/*
 * fatreferenceexecute(), longname version
 */

struct fatreferenceexecutelong {
	struct fatlongscan scan;
	refrunlong act;
	void *user;
};

int _fatreferenceexecutelong(fat *f,
		unit *directory, int index, int32_t previous,
		unit *startdirectory, int startindex, int32_t startprevious,
		unit *dirdirectory, int dirindex, int32_t dirprevious,
		int direction, void *user) {
	struct fatreferenceexecutelong *s;
	int res;

	s = (struct fatreferenceexecutelong *) user;

	if (direction == -1)
		fatlongend(&s->scan);
	
	if (directory == NULL)
		return FAT_REFERENCE_ALL |
			s->act(f, directory, index, previous,
				startdirectory, startindex, startprevious,
				dirdirectory, dirindex, dirprevious,
				NULL, 0, NULL, 0,
				direction, s->user);

	res = fatlongscan(directory, index, &s->scan);
	if (! (res & FAT_SHORT))
		return FAT_REFERENCE_NORMAL | FAT_REFERENCE_ALL;
	res = s->act(f, directory, index, previous,
		startdirectory, startindex, startprevious,
		dirdirectory, dirindex, dirprevious,
		s->scan.name, s->scan.err,
		s->scan.longdirectory, s->scan.longindex,
		direction, s->user);
	fatlongend(&s->scan);
	return FAT_REFERENCE_ALL | res;
}

int fatreferenceexecutelong(fat *f,
		unit *directory, int index, int32_t previous,
		refrunlong act, void *user) {
	struct fatreferenceexecutelong s;
	fatlonginit(&s.scan);
	s.act = act;
	s.user = user;
	return fatreferenceexecute(f, directory, index, previous,
		_fatreferenceexecutelong, &s);
}

/*
 * dump the structure of the entire filesystem
 */

struct fatdumplong {
	int level;
	int recur;
	int all;
	int32_t chain;
};

int _fatdumplong(fat __attribute__((unused)) *f,
		unit *directory, int index, int32_t previous,
		unit __attribute__((unused)) *startdirectory,
		int __attribute__((unused)) startindex,
		int32_t __attribute__((unused)) startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		wchar_t *name, int err,
		unit __attribute__((unused)) *longdirectory,
		int __attribute__((unused)) longindex,
		int direction, void *user) {
	struct fatdumplong *s;
	int32_t target;
	unit *scandirectory;
	int scanindex;
	int i;

	s = (struct fatdumplong *) user;

	if (direction == 1)
		s->level++;
	if (direction == -1)
		s->level--;

	if (direction != 0)
		return FAT_REFERENCE_COND(s->recur);
	
	target = fatreferencegettarget(f, directory, index, previous);

	if (fatreferenceiscluster(directory, index, previous)) {
		if (s->chain == FAT_ERR - 1)
			fatreferenceprint(directory, index, previous);
		else if (target != previous + 1) {
			if (previous == s->chain)
				printf(" %d", previous);
			else
				printf(" %d-%d", s->chain, previous);
			s->chain = target;
		}
	}
	else {
		if (! fatreferenceisentry(directory, index, previous))
			fatreferenceprint(directory, index, previous);
		if (s->chain != FAT_ERR - 1)
			s->chain = target;
	}

	if (! fatreferenceisentry(directory, index, previous)) {
		if (target == FAT_EOF ||
		    target == FAT_UNUSED ||
		    target == FAT_ERR)
			printf("\n");
		return FAT_REFERENCE_COND(s->recur);
	}

	scandirectory = s->all ? longdirectory : directory;
	scanindex = s->all ? longindex : index;

	do {
		fatentryprintpos(scandirectory, scanindex, 10);
		for (i = 0; i < s->level; i++)
			printf("    ");
		fatreferenceprint(scandirectory, scanindex, 0);
		if (fatentryislongpart(scandirectory, scanindex))
			printf("\n");
	} while ((scandirectory->n != directory->n || scanindex != index) &&
		 ! fatnextentry(f, &scandirectory, &scanindex));
	printf("  %-15ls %s", name, err == 0 ? "" : "ERR ");

	if (target == FAT_EOF || target == FAT_UNUSED || target == FAT_ERR)
		printf("\n");
	return FAT_REFERENCE_COND(s->recur);
}

void fatdumplong(fat *f, unit *directory, int index, int32_t previous,
		int recur, int all, int chains) {
	struct fatdumplong s;
	s.level = 0;
	s.recur = recur;
	s.all = all;
	s.chain = chains ? FAT_EOF : FAT_ERR - 1;
	fatreferenceexecutelong(f, directory, index, previous,
		_fatdumplong, &s);
}

/*
 * execute a callback on every file; it receives also the long name
 */

#define MAX_PATH (0xFFFF)

struct fatfilelong {
	void *user;
	wchar_t path[MAX_PATH + 1];
	wchar_t *name;
	longrun act;
};

int _fatfileexecutelong(fat *f,
		unit *directory, int index, int32_t previous,
		unit __attribute__((unused)) *startdirectory,
		int __attribute__((unused)) startindex,
		int32_t __attribute__((unused)) startprevious,
		unit __attribute__((unused)) *dirdirectory,
		int __attribute__((unused)) dirindex,
		int32_t __attribute__((unused)) dirprevious,
		wchar_t *name, int err, unit *longdirectory, int longindex,
		int direction, void *user) {
	struct fatfilelong *s;
	wchar_t *pos;

	s = (struct fatfilelong *) user;

	if (directory == NULL && previous == -1)
		return FAT_REFERENCE_RECUR | FAT_REFERENCE_DELETE;

	if (directory == NULL)
		return 0;

	switch (direction) {
	case 0:
		if (fatentryisdirectory(directory, index) &&
		    ! fatentryisdotfile(directory, index))
			s->name = wcsdup(name);
		break;
	case 1:
		wcsncat(s->path, s->name, MAX_PATH);
		wcsncat(s->path, L"/", MAX_PATH);
		free(s->name);
		s->name = NULL;
		return 0;
	case -1:
		s->path[wcslen(s->path) - 1] = L'\0';
		pos = wcsrchr(s->path, L'/');
		if (pos == NULL)
			s->path[0] = L'\0';
		else
			*(pos + 1) = L'\0';
		return 0;
	case -2:
		return 0;
	}

	s->act(f, s->path, directory, index,
		name, err, longdirectory, longindex, s->user);

	return FAT_REFERENCE_RECUR | FAT_REFERENCE_DELETE;
}

int fatfileexecutelong(fat *f, unit *directory, int index, int32_t previous,
		longrun act, void *user) {
	struct fatfilelong s;

	s.user = user;
	s.path[0] = L'\0';
	s.name = NULL;
	s.act = act;

	return fatreferenceexecutelong(f, directory, index, previous,
		_fatfileexecutelong, &s);
}

/*
 * from a directory entry to the start of its longname
 */
int fatshortentrytolong(fat *f, fatinverse *rev,
		unit *directory, int index,
		unit **longdirectory, int *longindex) {
	uint8_t checksum;
	uint8_t prog;
	int n;

	if (! fatentryexists(directory, index))
		return -1;

	if (fatentryislongpart(directory, index))
		return -1;

	checksum = fatentrychecksum(directory, index);

	for (n = 1; ! fatinversepreventry(f, rev, &directory, &index); n++) {
		if (! fatentryexists(directory, index))
			return -1;
		if (checksum != ENTRYPOS(directory, index, 13))
			return -1;
		prog = ENTRYPOS(directory, index, 0);
		if (n != (prog & 0x3F))
			return -1;
		if (prog & 0x40)
			break;
	}

	*longdirectory = directory;
	*longindex = index;
	return 0;
}

/*
 * from a cluster reference to the shortentry and the start of the long name
 */
int fatlongreferencetoentry(fat *f, fatinverse *rev,
		unit **directory, int *index, int32_t *previous,
		unit **longdirectory, int *longindex) {
	if (fatinversereferencetoentry(rev, directory, index, previous))
		return -2;

	return fatshortentrytolong(f, rev,
		*directory, *index, longdirectory, longindex);
}

/*
 * from a shortname entry to its possibly long name
 */
int fatshortentrytolongname(fat *f, fatinverse *rev,
		unit *directory, int index, wchar_t **longname) {
	unit *longdirectory;
	int longindex;

	if (fatshortentrytolong(f, rev, directory, index,
			&longdirectory, &longindex)) {
		longdirectory = directory;
		longindex = index;
	}

	return fatnextname(f, &longdirectory, &longindex, longname);
}

/*
 * from a cluster reference to its longname path
 */
wchar_t *fatinversepathlong(fat *f, fatinverse *rev,
		unit *directory, int index, int32_t previous) {
	wchar_t *path, *longname;
	int pathlen, namelen;

	path = NULL;
	pathlen = 0;

	while(! fatreferenceisvoid(directory, index, previous) &&
	      ! fatreferenceisboot(directory, index, previous)) {

		fatinversereferencetoentry(rev, &directory, &index, &previous);

		if (fatreferenceisboot(directory, index, previous))
			longname = wcsdup(L"");
		else if (fatreferenceisentry(directory, index, previous))
			fatshortentrytolongname(f, rev, directory, index,
				&longname);
		else
			return path;

		dprintf("%ls\n", longname[0] == L'\0' ? L"/" : longname);

		namelen = wcslen(longname);
		path = realloc(path,
			sizeof(wchar_t) * (pathlen + namelen + 1));
		wmemmove(path + namelen + 1, path, pathlen);
		path[namelen] = pathlen == 0 ? L'\0' : L'/';
		wmemmove(path, longname, namelen);
		pathlen = pathlen + 1 + namelen;

		free(longname);

		previous = directory == NULL ? 0 : directory->n;
		directory = NULL;
		index = 0;
	}

	return path;
}

