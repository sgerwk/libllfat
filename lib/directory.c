/*
 * directory.c
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "table.h"
#include "entry.h"
#include "directory.h"

int fatdirectorydebug = 0;
#define dprintf if (fatdirectorydebug) printf

/*
 * number of clusters of file
 */
int fatentrynumclusters(fat *f, unit *directory, int index) {
	return (fatentrygetsize(directory, index) +
			fatbytespercluster(f) - 1) /
		fatbytespercluster(f);
}

/*
 * next entry of a directory
 */
int fatnextentry(fat *f, unit **directory, int *index) {
	int32_t cn;

	(*index)++;

	if (*index >= (*directory)->size / 32) {
		*index = 0;
		cn = fatgetnextcluster(f, (*directory)->n);
		if (cn < FAT_FIRST) {
			*directory = NULL;
			return cn == FAT_EOF || cn == FAT_UNUSED ? -1 : -2;
		}
		*directory = fatclusterread(f, cn);
		if (*directory == NULL)
			return -3;
	}

	return fatentryend(*directory, *index);
}

/*
 * string matching, case sensitive or not depending on f->insensitive
 */
int _fatcmp(fat *f, const char *a, const char *b) {
	return f->insensitive ? strcasecmp(a, b) : strcmp(a, b);
}

/*
 * cluster/index pair of a file, given its short name
 */
int fatlookupfile(fat *f, int32_t dir,
		const char *shortname, unit **directory, int *index) {
	char name[13];
	unit *scandirectory;
	int scanindex;
	uint32_t cl;

	dprintf("lookup file %s:", shortname);

	if (sscanf(shortname, "ENTRY:%d,%d", &cl, index) == 2) {
		if (cl == 0)
			cl = fatgetrootbegin(f);
		*directory = fatclusterread(f, cl);
		return 0;
	}

	scandirectory = fatclusterread(f, dir);

	for (scanindex = -1; ! fatnextentry(f, &scandirectory, &scanindex); ) {
		dprintf(" %d,%d", (scandirectory)->n, scanindex);
		fatentrygetshortname(scandirectory, scanindex, name);
		if (! _fatcmp(f, name, shortname)) {
			*directory = scandirectory;
			*index = scanindex;
			dprintf(" (found)\n");
			return 0;
		}
	}

	dprintf(" (not found)\n");
	return -1;
}

/*
 * number of the first cluster of a file, given its short name
 */
int32_t fatlookupfirstcluster(fat *f, int32_t dir, const char *shortname) {
	unit *c;
	int index;
	int32_t cl;

	if (sscanf(shortname, "CLUSTER:%d", &cl) == 1)
		return cl;

	if (fatlookupfile(f, dir, shortname, &c, &index))
		return FAT_ERR;
	
	return fatentrygetfirstcluster(c, index, fatbits(f));
}

/*
 * cluster/index of file, given its path
 */
int fatlookuppath(fat *f, int32_t dir,
		const char *path, unit **directory, int *ind) {
	char *end, *copy;
	int res;

	dprintf("path=%s\n", path);

	end = strchr(path, '/');

	if (end == NULL)
		return fatlookupfile(f, dir, path, directory, ind);

	if (end == path) {
		return fatlookuppath(f, fatgetrootbegin(f),
			path + 1, directory, ind);
	}
	
	if (end[1] == '\0') {
		copy = strdup(path);
		copy[end - path] = '\0';
		res = fatlookuppath(f, dir, copy, directory, ind);
		free(copy);
		return res;
	}

	copy = strdup(path);
	copy[end - path] = '\0';
	dir = fatlookupfirstcluster(f, dir, copy);
	if (dir == 0)
		dir = fatgetrootbegin(f);
	if (dir == FAT_ERR) {
		dprintf("part of path not found: '%s'\n", copy);
		free(copy);
		*directory = NULL;
		return -1;
	}

	dprintf("name '%s', directory: %d\n", copy, dir);

	res = fatlookuppath(f, dir, copy + (end - path + 1),
		directory, ind);

	if (! res) {
		dprintf("name '%s':", copy + (end - path + 1));
		dprintf(" %d,%d\n", (*directory)->n, *ind);
	}

	free(copy);
	return res;
}

/*
 * first cluster of file, given its path
 */
int32_t fatlookuppathfirstcluster(fat *f, int32_t dir, const char *path) {
	unit *directory;
	int index;

	if (! strcmp(path, "/"))
		return fatgetrootbegin(f);

	if (! strchr(path, '/'))
		return fatlookupfirstcluster(f, dir, path);

	if (fatlookuppath(f, dir, path, &directory, &index))
		return FAT_ERR;

	return fatentrygetfirstcluster(directory, index, fatbits(f));
}

/*
 * find first available directory entry
 */
int fatfindfreeentry(fat *f, unit **directory, int *index) {
	unit *nextdirectory;
	int new;

	dprintf("searching for a free entry starting from %d,%d:",
		(*directory)->n, *index);

	for (nextdirectory = *directory;
	     ! fatnextentry(f, &nextdirectory, index);
	     *directory = nextdirectory) {
		dprintf(" %d,%d", nextdirectory->n, *index);
		if (! fatentryexists(nextdirectory, *index)) {
			dprintf(" (found)\n");
			return 0;
		}
	}

		/* loop ended because of a "directory end" entry */

	if (nextdirectory != NULL) {
		dprintf(" found\n");
		return 0;
	}

		/* cluster ended */

		/* root directory cannot be extended on fat12/fat16 */

	if ((*directory)->n == FAT_ROOT) {
		dprintf("fixed root directory, cannot extend it\n");
		*directory = NULL;
		return -1;
	}

		/* fat32: search for a free cluster */

	dprintf("searching for a free cluster\n");
	new = fatclusterfindfree(f);
	dprintf("free cluster found: %d\n", new);
	if (new == FAT_ERR) {
		*directory = NULL;
		return -1;
	}

		/* allocate the new cluster */

	dprintf("allocating a new cluster: %d\n", new);
	fatsetnextcluster(f, new, FAT_EOF);
	fatsetnextcluster(f, (*directory)->n, new);
	*directory = fatclustercreate(f, new);
	if (*directory == NULL)
		*directory = fatclusterread(f, new);
	*index = 0;

		/* fill it with unused directory entries */

	memset(fatunitgetdata(*directory), 0, fatbytespercluster(f));
	(*directory)->dirty = 1;
	return 0;
}

/*
 * find first free entry, given the path of the directory
 */
int fatfindfreeentrypath(fat *f, int32_t dir, const char *path,
		unit **directory, int *index) {
	int32_t cl, r;

	dprintf("path: %s\n", path);

	r = fatgetrootbegin(f);
	
	if (path == NULL || ! strcmp(path, "") || ! strcmp(path, "/"))
		cl = r;
	else {
		if (path[0] == '/')
			dir = r;
		cl = fatlookuppathfirstcluster(f, dir, path);
		if (cl == FAT_ERR)
			return FAT_ERR;
		if (cl == 0)
			cl = fatgetrootbegin(f);
	}

	dprintf("directory cluster: %d\n", cl);
	
	*directory = fatclusterread(f, cl);
	if (*directory == NULL)
		return -1;
	*index = -1;
	return fatfindfreeentry(f, directory, index);
}

/*
 * check whether a string is a valid shortname
 */
int fatinvalidname(const char *name) {
	char *dot;
	int i;

	if (name[0] == '\0' || name[0] == ' ')
		return -1;

	if (! strcmp(name, ".") || ! strcmp(name, ".."))
		return 1;

	dot = strchr(name, '.');

	if (dot == name)
		return -1;

	if (dot != NULL && dot != strrchr(name, '.'))
		return -1;
	
	if (dot != NULL && (dot - name > 8 || strlen(dot) > 3 + 1))
		return -1;

	if (dot != NULL && (dot[1] == '\0' || dot[1] == ' '))
		return -1;

	if (dot == NULL && strlen(name) > 8)
		return -1;

	for (i = 0; name[i]; i++) {
		if (name[i] < 0)
			continue;
		if (isalnum(name[i]))
			continue;
		if (strchr(". $%'-_@~`!(){}^#&", name[i]))
			continue;
		if ((unsigned char) name[i] > 127)
			continue;
		return -1;
	}

	return 0;
}

/*
 * check a path for validity
 */
int fatinvalidpath(const char *path) {
	char *copy, *scan, *sl;
	int res;

	res = 0;
	copy = strdup(path);
	for (scan = copy;
	     *scan && res >= 0;
	     scan = sl ? sl : scan + strlen(scan)) {
		for (sl = strchr(scan, '/'); sl && *sl == '/'; sl++)
			*sl = '\0';
		if (scan == copy && ! scan[0])
			continue;
		printf("part: |%s|\n", scan);
		res = fatinvalidname(scan);
	}

	free(copy);
	return *scan == '\0' ? res : -1;
}

/*
 * convert part of a string into the form that is used for a file name
 */
char *_fatstoragepart(char **dst, const char **src) {
	char *end, *start, *nonspace;

	start = *dst;

	end = strchr(*src, '/');
	end = end == NULL ? (char *) *src + strlen(*src) : end;

	nonspace = NULL;
	for ( ; **src != '\0' && **src != '/'; (*src)++) {
		if (**src == '.') {
			if (nonspace != NULL)
				*dst = nonspace;
			nonspace = NULL;
		}

		**dst = toupper(**src);

		if (**dst != ' ')
			nonspace = *dst + 1;
		(*dst)++;
	}
	if (nonspace != NULL)
		*dst = nonspace;
	**dst = **src;

	dprintf("start: |%s|\t\tsrc: |%s|\n", start, *src);
	return start;
}

/*
 * convert a string into the form that is used for a file name
 */
char *fatstoragename(const char *name) {
	char *dst, *start;
	dst = malloc(strlen(name) + 1);
	start = _fatstoragepart(&dst, &name);
	if (*dst == '/')
		printf("WARNING: path passed as file to %s()\n", __func__);
	return start;
}

/*
 * convert a string into the form that is used for a path
 */
char *fatstoragepath(const char *path) {
	char *start, *dst;
	start = malloc(strlen(path) + 1);
	for (dst = start; _fatstoragepart(&dst, &path) && *path; ) {
		path++;
		dst++;
	}
	return start;
}

/*
 * create a file given its path (return its directory,index pair)
 */
int fatcreatefile(fat *f, int32_t dir, char *path,
		unit **directory, int *index) {
	char *buf, *slash, *scan, *dirname, *file;

	if (path[0] == '\0')
		return -1;

	buf = strdup(path);

	for (scan = slash = strrchr(buf, '/');
	     scan != NULL && scan >= buf && *scan == '/';
	     scan--)
		*scan = '\0';

	if (slash == NULL) {
		dirname = "/";
		file = buf;
	}
	else if (scan == buf - 1) {
		dirname = "/";
		file = slash + 1;
	}
	else {
		dirname = buf;
		file = slash + 1;
	}

	printf("path %s\tfile %s\n", dirname, file);

	if (*file == '\0')
		return -1;

	if (fatfindfreeentrypath(f, dir, dirname, directory, index)) {
		dprintf("no free entry for file\n");
		free(buf);
		return -1;
	}

	dprintf("creating in %d,%d\n", (*directory)->n, *index);

	fatentryzero(*directory, *index);
	if (fatentrysetshortname(*directory, *index, file))
		return -1;
	fatentrysetsize(*directory, *index, 0);
	fatentrysetfirstcluster(*directory, *index, f->bits, FAT_UNUSED);

	free(buf);
	return 0;
}

